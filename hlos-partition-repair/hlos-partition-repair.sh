#! /bin/sh

# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

# Check common hlos partitions mounted or not.
# if not mounted, repair the partition and mount.

# configure all partitions name in this conf file
config_file="/etc/hlos-partition-repair.conf"

# set flag is partitions recovered
reboot_flag=0
exit_flag=0

set_userdata() {
    # mount userdata partition
    mount -t ext4 /dev/block/bootdevice/by-name/userdata /data_bkp -o rootcontext=system_u:object_r:bkp_data_t:s0
    st=$?
    chcon -t bkp_data_t /data_bkp

    if [[ $st -eq 0 ]]; then
        cp -rf --preserve=all /data/* /data_bkp/.
        sync
    fi
}

# Restore persist partiton data from persist_bkp
restore_persist() {
    restorecon -Fv /persist_bkp
    # Mount persist and persist_bkp partitions to restore existing backup data
    mount -t ext4 /dev/block/bootdevice/by-name/persist /persist -o rootcontext=system_u:object_r:persist_t:s0
    pst=$?
    mount -t ext4 /dev/block/bootdevice/by-name/persist_bkp /persist_bkp -o rootcontext=system_u:object_r:persist_t:s0
    pbst=$?
    chcon -t persist_t /persist

    # Change directory to /persist
    cd /persist
    if [ -e /persist_bkp/persist.dump -a -f /persist_bkp/persist.dump ]; then
        rm -rf lost+found
        # Restore existing data
        restore -rf /persist_bkp/persist.dump
        echo "restore complete with exit code $0"
        sync
    fi
}

# Repair hlos common partitions
repair_hlos_part() {

    # Parse partition path
    if [ "$1" = "data" ] ; then
        part_name=/dev/block/bootdevice/by-name/user$1
    else
        part_name=/dev/block/bootdevice/by-name/$1
    fi

    # create new file-system for vfat
    if [ "$1" = "manifest_a" -o "$1" = "manifest_b" ]; then
         echo "Repairing vfat $part_name"
         fsck.vfat -pw $part_name
         status=$?
         if [[ $status -eq 0 ]]; then
            echo "$1 has been clean"
            reboot_flag=1
            return
         else
            echo "Fail to repair $1, Creating new FS"
            mkfs.vfat $part_name
            if [[ $? -eq 0 ]]; then
                reboot_flag=1
                echo "Created new FS $1"
                return
            else
                echo "Fail to create FS $1"
                exit_flag=1
                return
            fi
         fi
    fi

    # Repair & clean partition
    echo "Repairing partition $part_name"
    e2fsck -pf $part_name
    status=$?

    # Create new file-system if partition repair fail
    if [[ $status -lt 3 ]]; then
         echo "$1 has been clean"
         if [ "$1" != "cache" ]; then
             reboot_flag=1
         fi
    elif [ $status -eq 4 -o $status -eq 8 ]; then
         echo "It's $part_name"
         mkfs.ext4 -F  $part_name
	 if [[ $? != 0 ]]; then
	     echo "Fail to repair $1"
             exit_flag=1
	     return
	 fi
         resize2fs $part_name
	 if [[ $? != 0 ]]; then
	     echo "Fail to resize $1"
	 fi

         # only set reboot flag if it's not Cache
         if [ "$1" != "cache" -a "$?" -eq 0 ]; then
             reboot_flag=1
         fi

         # restore persist from persist_bkp partition
         if [ "$1" = "persist" ] ; then
             restore_persist
         fi
         # restore userdata content as default
         if [ "$1" = "data" -a "$?" -eq 0 ] ; then
            set_userdata
         fi
    else
         echo "can not repair\ne2fsck not working correctly."
	 exit_flag=1
    fi

}

# Repair every partition mentioned in hlos-partition-repair.conf
for partition in $(cat $config_file); do
    if mount | grep -q "$partition "; then
        echo "$partition is mounted"
    else
        echo "$partition is not mounted"
        echo "Starting repair for $partition"
        repair_hlos_part "$partition"
    fi
done

# Reboot device in case of any common hlos partition recovered/repaired.
if [ $reboot_flag -eq 1 -a $exit_flag -eq 0 ]; then
    echo "Reboot required "
    /sbin/reboot
else
    echo "Reboot not required"
    exit $exit_flag
fi

