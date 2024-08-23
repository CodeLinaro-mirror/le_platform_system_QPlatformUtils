#! /bin/sh
#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

# Take backup of persist partition in persist_bkp partition


restorecon -vF /persist_bkp/
MNT_POINT="/persist"

if mount | grep -q "$MNT_POINT" ;
then
   # Check if persist partition is clean
   e2fsck -n /dev/block/bootdevice/by-name/persist
   fsck_status=$?
   if [[ $fsck_status -lt 2 ]]; then
       mount -t ext4 /dev/block/bootdevice/by-name/persist_bkp /persist_bkp -o rootcontext=system_u:object_r:persist_t:s0
       st=$?

       if [[ $st -eq 0 ]]; then
           # Store Persist partition backup in persist_bkp partition
           dump -0f /persist_bkp/persist.dump /dev/block/bootdevice/by-name/persist > /dev/null 2>&1
           umount /persist_bkp
           echo "persist backup finished"
       else
           echo "persist_bkp mount fail"
           exit 1
       fi
   else
        echo "persist corrupted, can't take backup $fsck_status"
        exit 1
   fi
else
   echo "persist is not mounted"
fi

