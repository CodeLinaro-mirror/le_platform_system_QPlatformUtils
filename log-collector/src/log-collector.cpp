/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */

#include "socket-connection.h"
#include "ssh-connection.h"
#include "log.h"

#include <json/json.h>
#include <fstream>
#include <iomanip>
#include <fcntl.h>

#include <filesystem>
namespace fs = std::filesystem;

#define CONF_FILE ("/etc/log-conf.json")
#define LOCAL_TEMP_DIR "/tmp/"

Json::Value Conf;

static std::string get_timestamp()
{
	std::time_t t = std::time(nullptr);
	std::tm tm{};
	localtime_r(&t, &tm);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
	return oss.str();
}

static bool perform_authentication(SSH::SSH& ssh)
{
	const bool byPassword = Conf["authentication"]["by_password"].asBool();
	std::string user      = Conf["authentication"]["credentials"]["user"].asString();
	std::string password  = Conf["authentication"]["credentials"]["password"].asString();
	std::string pubKey    = Conf["authentication"]["ssh_keys"]["public"].asString();
	std::string privKey   = Conf["authentication"]["ssh_keys"]["private"].asString();

	if (byPassword) {
		return ssh.authenticate_session(user.c_str(), true, password.c_str(), nullptr, nullptr);
	} else {
		return ssh.authenticate_session(user.c_str(), false, password.c_str(), pubKey.c_str(), privKey.c_str());
	}
}

static std::string append_timestamp_to_remotedir(const std::string& baseRemotePath)
{
	const std::string runTimestamp = get_timestamp();

	std::string base = baseRemotePath;

	if (!base.empty() && base.back() != '/')
		base.push_back('/');

	return baseRemotePath + "log-collector-" + runTimestamp + "/";
}

static int handle_dmesg(SSH::SSH& ssh, const std::string& targetDir)
{
	LOGI("dmesg_logs enabled - Transferring dmesg log file");
	const std::string localPath  = "/dev/kmsg";
	const std::string remoteFile = targetDir + "dmesg.log";
	return ssh.sftp_transfer_file(localPath, remoteFile, O_NONBLOCK);
}

static int handle_logread(SSH::SSH& ssh, const std::string& targetDir)
{
	LOGI("logread enabled - Transferring logread logs");
	const std::string logType   = "logread";
	const std::string localFile = std::string(LOCAL_TEMP_DIR) + logType + ".log";

	std::string cmd = "logread > " + localFile;
	int rc = system(cmd.c_str());

	if (rc != 0) {
		LOGE("logread command failed");
		remove(localFile.c_str());
		return -1;
	}
	const std::string remoteFile = targetDir + logType + ".log";
	int xfer = ssh.sftp_transfer_file(localFile, remoteFile);
	remove(localFile.c_str());
	return xfer;
}

static int handle_cnss_diag(SSH::SSH& ssh, const std::string& targetDir)
{
	bool enabled = false;
	int duration_sec = 30;
	const std::string out_path = "/tmp/cnss_diag_opt.txt";

	const Json::Value& cnss = Conf["log_transfer"]["sources"]["cnss_diag"];

	if (cnss.isObject()) {
		duration_sec = cnss.get("duration_sec", duration_sec).asInt();
	}

	if (duration_sec <= 0) {
		LOGE("cnss_diag: invalid duration_sec <= 0");
		return -1;
	}

	LOGI("cnss_diag enabled - capturing for " << duration_sec << "s to " << out_path);

	// Clear previous file
	remove(out_path.c_str());

	{
		std::string cmd =
			"sh -c 'cnss_diag -c > " + out_path +
			" & pid=$!; sleep " + std::to_string(duration_sec) +
			"; kill -INT $pid; wait $pid'";
		int rc = system(cmd.c_str());
		if (rc != 0) {
			LOGE("cnss_diag command sequence failed (rc=" << rc << ")");
			return -1;
		}
	}

	const std::string remoteFile = targetDir + "cnss_diag.txt";
	int xfer = ssh.sftp_transfer_file(out_path, remoteFile);
	remove(out_path.c_str());
	return xfer;
}

static int handle_ipc_logging(SSH::SSH& ssh, const std::string& targetDir)
{
	const std::vector<std::pair<std::string, std::string>> sources = {
		{ "/sys/kernel/debug/ipc_logging/ioss/log",  "ipc_logging_ioss.log"   },
		{ "/sys/kernel/debug/ipc_logging/eth_qos/log","ipc_logging_eth_qos.log"},
		{ "/sys/kernel/debug/ipc_logging/emac/log",   "ipc_logging_emac.log"   }
	};

	int overall_rc = 0;
	LOGI("ipc_logs enabled - Transferring IPC logging debugfs files");

	for (const auto& kv : sources) {
		const std::string& localPath  = kv.first;
		const std::string& remoteName = kv.second;
		const std::string  remotePath = targetDir + remoteName;

		int rc = ssh.sftp_transfer_file(localPath, remotePath);
		if (rc != 0) {
			LOGE("ipc_logs: failed to transfer " << localPath.c_str() << " -> " << remotePath.c_str());
			if (overall_rc == 0) overall_rc = rc;
		} else {
			LOGI("ipc_logs: transferred " << localPath.c_str() << " -> " << remotePath.c_str());
		}
	}
	return overall_rc;
}

static int clean_data_logs()
{
	int rc = system(
			"sh -c '"
			"rm -f /data/logs/* 2>/dev/null; "
			"find /data/logs -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} + 2>/dev/null || true'"
			);

	if (rc != 0) {
		LOGE(std::string("Cleanup of /data/logs encountered non-zero rc=") + std::to_string(rc));
	} else {
		LOGI("Cleanup: cleared contents of /data/logs");
	}
	return rc;
}

static int append_file(const std::string& src, const std::string& dst, mode_t create_mode = 0644)
{
	struct stat st{};
	if (stat(src.c_str(), &st) != 0) return -errno;

	std::ifstream in(src, std::ios::in | std::ios::binary);

	if (!in) return -errno;

	if (access(dst.c_str(), F_OK) != 0) {
		int fd = ::creat(dst.c_str(), create_mode);
		if (fd < 0) return -errno;
		::close(fd);
	}

	std::ofstream out(dst, std::ios::out | std::ios::binary | std::ios::app);
	if (!out) return -errno;

	out << in.rdbuf();

	if (!out.good()) return -EIO;

	return 0;
}

static bool write_string_to_file(const fs::path& p, const std::string& s)
{
	std::ofstream out(p, std::ios::out | std::ios::trunc);
	if (!out) {
		std::cerr << "ERROR: Cannot open " << p << " for writing.\n";
		return false;
	}
	out << s;
	out.flush();
	if (!out) {
		std::cerr << "ERROR: Failed to write to " << p << ".\n";
		return false;
	}
	return true;
}

static int handle_ipa_logging(SSH::SSH& ssh, const std::string& targetDir)
{
	const Json::Value& srcs = Conf["log_transfer"]["sources"];
	const Json::Value& ipa  = srcs["ipa_logging"];
	const bool ipa_logging_enabled = ipa.get("enabled", false).asBool();
	const Json::Value& opts = ipa["options"];

	const bool tcpdump_enabled   = opts.get("tcpdump",   false).asBool();
	const bool ip_diag_enabled   = opts.get("ip_diag",   false).asBool();
	const bool ipa_rules_enabled = opts.get("ipa_rules", false).asBool();
	const bool firmware_enabled  = opts.get("firmware",  false).asBool();

	if (!ipa_logging_enabled) {
		return 0;
	}

	// Create Remote directory log-collector_<timestamp>/ipa_logs/
	const std::string remoteIpaDir = targetDir + "ipa_logs/";

	auto exists = [](const std::string& p) -> bool {
		return ::access(p.c_str(), F_OK) == 0;
	};

	auto starts_with = [](const std::string& s, const char* pfx) -> bool {
		return s.rfind(pfx, 0) == 0;
	};

	auto is_nonempty = [](const std::string& p) -> bool {
		struct stat st{};
		return ::stat(p.c_str(), &st) == 0 && st.st_size > 0;
	};

	auto transfer_and_cleanup = [&](const std::string& local) -> int {
		if (!exists(local)) {
			LOGI(std::string("skip xfer (missing): ") + local);
			return 0;
		}
		if (!is_nonempty(local)) {
			LOGI(std::string("skip xfer (empty): ") + local);
			return 0;
		}
		const size_t slash = local.find_last_of('/');
		const std::string name = (slash == std::string::npos) ? local : local.substr(slash + 1);
		const std::string remote = remoteIpaDir + name;
		int rc = ssh.sftp_transfer_file(local, remote);

		if (rc != 0) {
			LOGE(std::string("xfer failed: ") + local + " -> " + remote + " (rc=" + std::to_string(rc) + ")");
		} else {
			LOGI(std::string("xfer ok: ") + local + " -> " + remote);
			if (starts_with(local, "/data/logs/")) {
				if (::remove(local.c_str()) == 0) {
					LOGI(std::string("deleted local: ") + local);
				} else {
					LOGE(std::string("failed to delete local: ") + local + " (errno=" + std::to_string(errno) + ")");
				}
			}
		}
		return rc;
	};

	auto vec_contains = [](const std::vector<std::string>& v, const std::string& s) -> bool {
		for (const auto& e : v) if (e == s) return true;
		return false;
	};

	// Ensure local /data/logs exists
	if (system("mkdir -p /data/logs") != 0) {
		LOGE("Failed to create /data/logs");
		return -1;
	}

	// Pre-Clean /data/logs
	(void)clean_data_logs();

	// ifconfig
	const std::string ifcfg_txt = "/data/logs/ifconfig.txt";
	(void)system("ifconfig > /data/logs/ifconfig.txt 2>/dev/null");

	// Parse interfaces and print
	auto parse_ifaces = [&](const std::string& path) -> std::vector<std::string> {
		std::vector<std::string> ifaces;
		std::ifstream in(path);
		if (!in.is_open()) {
			LOGE(std::string("Failed to open ") + path + " for reading");
			return ifaces;
		}
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream iss(line);
			std::string first, second;
			if (!(iss >> first >> second)) continue;
			if (!first.empty() && first.back() == ':') first.pop_back();

			if (starts_with(second, "flags=") || starts_with(second, "Link")) {
				if (!first.empty() && !vec_contains(ifaces, first)) {
					ifaces.push_back(first);
				}
			}
		}
		in.close();

		// Log results
		std::string joined;
		for (size_t i = 0; i < ifaces.size(); ++i) {
			if (i) joined += ' ';
			joined += ifaces[i];
		}
		LOGI(std::string("Collected interfaces: ") + joined);
		for (const auto& i : ifaces) LOGI(std::string("iface: ") + i);
		return ifaces;
	};

	std::vector<std::string> ifaces = parse_ifaces(ifcfg_txt);

	// Track tcpdump pids/files if enabled
	std::vector<std::pair<std::string,int>> tcpdump_pids;
	std::vector<std::string> pid_files;

	// If tcpdump enabled start tcpdump per interface
	if (tcpdump_enabled) {
		LOGI("tcpdump enabled - starting per-interface captures (-c500)");
		for (const auto& i : ifaces) {
			const std::string pidf = "/tmp/tcpdump_" + i + ".pid";
			pid_files.push_back(pidf);
			std::string cmd =
				"sh -c 'tcpdump -vnei " + i + " -c500 -w /data/logs/" + i +
				".pcap >/dev/null 2>&1 & echo $! > " + pidf + "'";

			int rc = system(cmd.c_str());

			if (rc != 0) {
				LOGE(std::string("Failed to start tcpdump on ") + i + " (rc=" + std::to_string(rc) + ")");
			} else {
				int p = 0; std::ifstream in(pidf); if (in.is_open()) in >> p;
				if (p > 0) {
					tcpdump_pids.emplace_back(i, p);
					LOGI(std::string("tcpdump started on ") + i + " (pid=" + std::to_string(p) + ")");
				} else {
					LOGE(std::string("tcpdump failed to get valid pid on ") + i);
				}
			}
		}
	}
	//Transfer and clear ifconfig.txt
	(void)transfer_and_cleanup(ifcfg_txt);

	int overall_rc = 0;

        // If ipa_logging is true default set
        if (ipa_logging_enabled ) {
                LOGI("ipa_logs enabled, Transfering  default logs");
                (void)system("bridge fdb show | tee -a /data/logs/fdb_show.txt > /dev/null");
		(void)append_file("/etc/data/ipa/IPACM_cfg.xml", "/data/logs/ipacm_cfg.xml");
		(void)append_file("/data/ipacm_log.txt", "/data/logs/ipacm_log.txt");
		(void)append_file("/data/data_ipa/ipacm_log.txt", "/data/logs/ipacm_log.txt");
		(void)append_file("/var/run/data/ipa/ipacm_log.txt", "/data/logs/ipacm_log.txt");
		(void)append_file("/sys/kernel/debug/ipc_logging/ipa/log", "/data/logs/ipa_ipc_logs.txt");
		(void)append_file("/sys/kernel/debug/ipc_logging/gsi/log", "/data/logs/gsi_ipc_logs.txt");
		(void)append_file("/sys/kernel/debug/ipc_logging/ipa_clk/log", "/data/logs/ipa_clk_logs.txt");

		std::vector<std::string> logs = {
			"/data/logs/fdb_show.txt",
                        "/data/logs/ipacm_cfg.xml",
                        "/data/logs/ipacm_log.txt",
                        "/data/logs/ipa_ipc_logs.txt",
                        "/data/logs/gsi_ipc_logs.txt",
                        "/data/logs/ipa_clk_logs.txt"
                };

                for (const auto& f : logs) {
                        int rc = transfer_and_cleanup(f);
                        if (rc != 0 && overall_rc == 0) overall_rc = rc;
                }
        }

	// ip_diag
	if (ip_diag_enabled) {
		LOGI("ip_diag enabled - collecting");
		(void)system("iptables-save | tee -a /data/logs/iptables_save.txt > /dev/null");
		(void)system("iptables -L -vn | tee -a /data/logs/iptables.txt > /dev/null");
		(void)system("ip6tables -L -vn | tee -a /data/logs/ip6tables.txt > /dev/null");
		(void)system("ip r s | tee -a /data/logs/iproutes.txt > /dev/null");
		(void)system("ip -6 r s | tee -a /data/logs/ip6routes.txt > /dev/null");
		(void)system("ip n s | tee -a /data/logs/ipneighs.txt > /dev/null");
		(void)system("ip -6 n s | tee -a /data/logs/ip6neighs.txt > /dev/null");
		(void)system("brctl show | tee -a /data/logs/brctl.txt > /dev/null");
		(void)system("conntrack -L | tee -a /data/logs/conntrack.txt > /dev/null");
		(void)system("conntrack -L --family ipv6 | tee -a /data/logs/conntrack_v6.txt > /dev/null");

		std::vector<std::string> logs = {
			"/data/logs/iptables_save.txt",
			"/data/logs/iptables.txt",
			"/data/logs/ip6tables.txt",
			"/data/logs/iproutes.txt",
			"/data/logs/ip6routes.txt",
			"/data/logs/ipneighs.txt",
			"/data/logs/ip6neighs.txt",
			"/data/logs/brctl.txt",
			"/data/logs/conntrack.txt",
			"/data/logs/conntrack_v6.txt"
		};
		for (const auto& f : logs) {
			int rc = transfer_and_cleanup(f);
			if (rc != 0 && overall_rc == 0) overall_rc = rc;
		}
	}

	// ipa_rules
	if (ipa_rules_enabled) {
		LOGI("ipa_rules enabled - collecting");
		if (exists("/sys/kernel/debug/")) {
			LOGI("------Debug enabled collecting debug logs--------");

			(void)append_file("/sys/kernel/debug/ipa/stats", "/data/logs/ipa_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/stats", "/data/logs/ipa_stats1.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/stats", "/data/logs/ipa_stats2.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/stats", "/data/logs/ipa_stats3.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/pm_stats", "/data/logs/pm_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/mhip_gsi_stats","/data/logs/mhip_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/odlstats", "/data/logs/odlstats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/usb_gsi_stats", "/data/logs/usb_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/wdi_gsi_stats", "/data/logs/wdi_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/wdi3_gsi_stats","/data/logs/wdi3_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/debug/ipa/msg", "/data/logs/ipa_msg.txt");


			(void)system("dmesg -c >> /data/logs/dmesg.txt");
			(void)system("cat /sys/kernel/debug/ipa/hdr >/dev/null 2>&1");
			(void)system("dmesg -c >> /data/logs/ipa_hdr.txt");

			(void)system("cat /sys/kernel/debug/ipa/proc_ctx >/dev/null 2>&1");
			(void)system("dmesg -c >> /data/logs/proc_ctx.txt");

			(void)system("cat /sys/kernel/debug/ipa/msg >> /data/logs/ipa_msg.txt");

			(void)system("cat /sys/kernel/debug/ipa/status_stats");
			(void)system("dmesg -c >> /data/logs/ipa_status_stats1.txt");

			(void)system("cat /sys/kernel/debug/ipa/status_stats");
			(void)system("dmesg -c >> /data/logs/ipa_status_stats2.txt");

			(void)system("cat /sys/kernel/debug/ipa/ipv6ct");
			(void)system("dmesg -c >> /data/logs/ipv6ct.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip4_flt");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_flt.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip4_flt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_flt_hw.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip4_rt");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_rt.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip4_nat");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_nat.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip6_flt");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_flt.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip6_flt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_flt_hw.txt");

			(void)system("cat /sys/kernel/debug/ipa/ip6_rt");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_rt.txt");

			(void)append_file("/sys/kernel/debug/ipa/stats", "/data/logs/ipa_stats_final.txt");
			(void)append_file("/sys/kernel/debug/ipa/ipsec_active_sa", "/data/logs/ipsec_active_sa.txt");

			// Conditional IPsec reads (debugfs)
			{
				const fs::path idx_node   = "/sys/kernel/debug/ipa/ipsec_set_sa_info_index";
				const fs::path encap_node = "/sys/kernel/debug/ipa/ipsec_encap_sa_info";
				const fs::path decap_node = "/sys/kernel/debug/ipa/ipsec_decap_sa_info";

				if (::access(idx_node.c_str(), F_OK) == 0) {
					for (int j = 0; j <= 10; ++j) {
						// echo -n $j > /sys/kernel/debug/ipa/ipsec_set_sa_info_index
						if (!write_string_to_file(idx_node, std::to_string(j))) {
							LOGE(std::string("ipsec(debugfs): failed to set index ") + std::to_string(j));
							continue;
						}

						// cat ipsec_encap_sa_info >> /data/logs/ipsec_encap$j.txt
						const fs::path encap_log = fs::path("/data/logs") / (std::string("ipsec_encap") + std::to_string(j) + ".txt");
						(void)append_file(encap_node.string(), encap_log.string());

						// cat ipsec_decap_sa_info >> /data/logs/ipsec_decap$j.txt
						const fs::path decap_log = fs::path("/data/logs") / (std::string("ipsec_decap") + std::to_string(j) + ".txt");
						(void)append_file(decap_node.string(), decap_log.string());
					}
				}
			}
			(void)system("eth-qos show eth0 >> /data/logs/eth_qos_eth0.txt");
			(void)system("eth-qos show eth1 >> /data/logs/eth_qos_eth1.txt");
			(void)append_file("/sys/kernel/debug/ipa/hw_stats/tethering", "/data/logs/hw_stats.txt");
			(void)append_file("/sys/kernel/debug/ipa/hw_stats/drop", "/data/logs/drop_stats.txt");
			(void)append_file("/sys/kernel/debug/ipa/eth/IEMAC_0_qos_stats", "/data/logs/eth_iemac0.txt");
			(void)append_file("/sys/kernel/debug/ipa/eth/IEMAC_1_qos_stats", "/data/logs/eth_iemac1.txt");

			(void)append_file("/etc/data/mobileap_cfg.xml", "/data/logs/mobile_cfg.xml");
			(void)append_file("/etc/data/ipa_config.txt", "/data/logs/ipa_config.txt");

		} else {
			LOGI("---Started collecting Non-debug Kernel ipa logs---");

			(void)append_file("/sys/kernel/ipa/stats", "/data/logs/ipa_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/stats", "/data/logs/ipa_stats1.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/stats", "/data/logs/ipa_stats2.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/stats", "/data/logs/ipa_stats3.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/pm_stats", "/data/logs/pm_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/mhip_gsi_stats", "/data/logs/mhip_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/odlstats", "/data/logs/odlstats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/usb_gsi_stats", "/data/logs/usb_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/wdi_gsi_stats", "/data/logs/wdi_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/wdi3_gsi_stats", "/data/logs/wdi3_gsi_stats.txt"); ::sleep(5);
			(void)append_file("/sys/kernel/ipa/ntn", "/data/logs/ntn.txt"); ::sleep(5);

			(void)append_file("/sys/kernel/ipa/ipa_dscp_pcp_mapping_cache", "/data/logs/ipa_dscp_pcp_mapping_cache.txt");
			(void)append_file("/sys/kernel/ipa/aqc_0_err_status", "/data/logs/aqc_0_err_status.txt");
			(void)append_file("/sys/kernel/ipa/enable_clock_scaling", "/data/logs/enable_clock_scaling.txt");
			(void)append_file("/sys/kernel/ipa/ntn_perf_status", "/data/logs/ntn_perf_status.txt");
			(void)append_file("/sys/kernel/ipa/tx_wrapper_cache_max_size", "/data/logs/tx_wrapper_cache_max_size.txt");
			(void)append_file("/sys/kernel/ipa/rtk_0_err_status", "/data/logs/rtk_0_err_status.txt");

			(void)append_file("/sys/kernel/ipa/page_poll_threshold", "/data/logs/page_poll_threshold.txt");
			(void)append_file("/sys/kernel/ipa/keep_awake", "/data/logs/keep_awake.txt");
			(void)append_file("/sys/kernel/ipa/page_recycle_stats", "/data/logs/page_recycle_stats.txt");
			(void)append_file("/sys/kernel/ipa/clock_scaling_bw_threshold_turbo_mbps", "/data/logs/clock_scaling_bw_threshold_turbo_mbps.txt");
			(void)append_file("/sys/kernel/ipa/clock_scaling_bw_threshold_nominal_mbps", "/data/logs/clock_scaling_bw_threshold_nominal_mbps.txt");
			(void)append_file("/sys/kernel/ipa/lan_coal_stats", "/data/logs/lan_coal_stats.txt");
			(void)append_file("/sys/kernel/ipa/enable_napi_chain", "/data/logs/enable_napi_chain.txt");
			(void)append_file("/sys/kernel/ipa/page_wq_reschd_time", "/data/logs/page_wq_reschd_time.txt");
			(void)append_file("/sys/kernel/ipa/iemac_1_err_status", "/data/logs/iemac_1_err_status.txt");
			(void)append_file("/sys/kernel/ipa/mpm_ring_size_dl", "/data/logs/mpm_ring_size_dl.txt");
			(void)append_file("/sys/kernel/ipa/ipa_max_napi_sort_page_thrshld", "/data/logs/ipa_max_napi_sort_page_thrshld.txt");
			(void)append_file("/sys/kernel/ipa/ntn_1_err_status", "/data/logs/ntn_1_err_status.txt");
			(void)append_file("/sys/kernel/ipa/mpm_ring_size_ul", "/data/logs/mpm_ring_size_ul.txt");
			(void)append_file("/sys/kernel/ipa/ntn3_1_err_status", "/data/logs/ntn3_1_err_status.txt");
			(void)append_file("/sys/kernel/ipa/msg", "/data/logs/msg.txt");
			(void)append_file("/sys/kernel/ipa/mpm_teth_aggr_size", "/data/logs/mpm_teth_aggr_size.txt");
			(void)append_file("/sys/kernel/ipa/cache_recycle_stats", "/data/logs/cache_recycle_stats.txt");
			(void)append_file("/sys/kernel/ipa/pm_ex_stats", "/data/logs/pm_ex_stats.txt");

			(void)append_file("/sys/kernel/ipa/ep_reg", "/data/logs/ep_reg.txt");
			(void)append_file("/sys/kernel/ipa/aqc_1_err_status", "/data/logs/aqc_1_err_status.txt");
			(void)append_file("/sys/kernel/ipa/hw_type", "/data/logs/hw_type.txt");
			(void)append_file("/sys/kernel/ipa/clk_rate", "/data/logs/clk_rate.txt");
			(void)append_file("/sys/kernel/ipa/rtk_1_err_status", "/data/logs/rtk_1_err_status.txt");
			(void)append_file("/sys/kernel/ipa/iemac_0_err_status", "/data/logs/iemac_0_err_status.txt");
			(void)append_file("/sys/kernel/ipa/wdi", "/data/logs/wdi.txt");
			(void)append_file("/sys/kernel/ipa/gen_reg", "/data/logs/gen_reg.txt");
			(void)append_file("/sys/kernel/ipa/ntn_0_err_status", "/data/logs/ntn_0_err_status.txt");
			(void)append_file("/sys/kernel/ipa/ntn3_0_err_status", "/data/logs/ntn3_0_err_status.txt");
			(void)append_file("/sys/kernel/ipa/wstats", "/data/logs/wstats.txt");
			(void)append_file("/sys/kernel/ipa/mpm_uc_thresh", "/data/logs/mpm_uc_thresh.txt");
			(void)append_file("/sys/kernel/ipa/app_clk_vote_cnt", "/data/logs/app_clk_vote_cnt.txt");
			(void)append_file("/sys/kernel/ipa/eth/eth_status", "/data/logs/eth_status.txt");

			(void)system("eth-qos show eth0 >> /data/logs/eth_qos_eth0.txt");
			(void)system("eth-qos show eth1 >> /data/logs/eth_qos_eth1.txt");
			(void)append_file("/sys/kernel/hw_stats/tethering", "/data/logs/hw_tethering_stats.txt");
			(void)append_file("/sys/kernel/hw_stats/drop", "/data/logs/drop_stats.txt");
			(void)append_file("/sys/kernel/ipa/eth/IEMAC_0_qos_stats", "/data/logs/eth_iemac0.txt");
			(void)append_file("/sys/kernel/ipa/eth/IEMAC_1_qos_stats", "/data/logs/eth_iemac1.txt");
			(void)append_file("/sys/kernel/gsi/gsi_fw_version", "/data/logs/gsi_fw_version.txt");
			(void)append_file("/sys/kernel/gsi/gsi_hw_profiling_stats", "/data/logs/gsi_hw_profiling_stats.txt");

			(void)system("dmesg -c >> /data/logs/dmesg.txt");

			(void)system("cat /sys/kernel/ipa/hdr");
			(void)system("dmesg -c >> /data/logs/ipa_hdr.txt");

			(void)system("cat /sys/kernel/ipa/ipa_dump_regs");
			(void)system("dmesg -c >> /data/logs/ipa_dump_regs.txt");

			(void)system("cat /sys/kernel/ipa/ip4_rt");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_rt.txt");

			(void)system("cat /sys/kernel/ipa/ip4_nat");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_nat.txt");

			(void)system("cat /sys/kernel/ipa/ip4_flt");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_flt.txt");

			(void)system("cat /sys/kernel/ipa/ip4_rt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_rt_hw.txt");

			(void)system("cat /sys/kernel/ipa/ip4_flt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip4_flt_hw.txt");

			(void)system("cat /sys/kernel/ipa/proc_ctx");
			(void)system("dmesg -c >> /data/logs/proc_ctx.txt");

			(void)system("cat /sys/kernel/ipa/status_stats");
			(void)system("dmesg -c >> /data/logs/ipa_status_stats1.txt");

			(void)system("cat /sys/kernel/ipa/status_stats");
			(void)system("dmesg -c >> /data/logs/ipa_status_stats2.txt");

			(void)system("cat /sys/kernel/ipa/ip6_rt");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_rt.txt");

			(void)system("cat /sys/kernel/ipa/ipv6ct");
			(void)system("dmesg -c >> /data/logs/ipv6ct.txt");

			(void)system("cat /sys/kernel/ipa/ip6_flt");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_flt.txt");

			(void)system("cat /sys/kernel/ipa/ip6_rt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_rt_hw.txt");

			(void)system("cat /sys/kernel/ipa/ip6_flt_hw");
			(void)system("dmesg -c >> /data/logs/ipa_ip6_flt_hw.txt");

			(void)system("dmesg -c >> /data/logs/dmesg1.txt");
			(void)system("cat /sys/kernel/ipa/stats >> /data/logs/ipa_stats_final.txt");

			(void)append_file("/etc/data/mobileap_cfg.xml", "/data/logs/mobile_cfg.xml");
			(void)append_file("/etc/data/ipa_config.txt", "/data/logs/ipa_config.txt");

			// Conditional IPsec reads in non-debugfs path
			(void)system("sh -c 'if [ -e /sys/kernel/debug/ipa/ipsec_active_sa ]; then "
					"cat /sys/kernel/debug/ipa/ipsec_active_sa >> /data/logs/ipsec_active_sa.txt; fi'");

                        {
                                const fs::path idx_node   = "/sys/kernel/ipa/ipsec_set_sa_info_index";
                                const fs::path encap_node = "/sys/kernel/ipa/ipsec_encap_sa_info";
                                const fs::path decap_node = "/sys/kernel/ipa/ipsec_decap_sa_info";

                                if (::access(idx_node.c_str(), F_OK) == 0) {
                                        for (int j = 0; j <= 10; ++j) {
                                                // echo -n $j > /sys/kernel/ipa/ipsec_set_sa_info_index
                                                if (!write_string_to_file(idx_node, std::to_string(j))) {
                                                        LOGE(std::string("ipsec(nodebugfs): failed to set index ") + std::to_string(j));
                                                        continue;
                                                }

                                                // cat ipsec_encap_sa_info >> /data/logs/ipsec_encap$j.txt
                                                const fs::path encap_log = fs::path("/data/logs") / (std::string("ipsec_encap") + std::to_string(j) + ".txt");
                                                (void)append_file(encap_node.string(), encap_log.string());

                                                // cat ipsec_decap_sa_info >> /data/logs/ipsec_decap$j.txt
                                                const fs::path decap_log = fs::path("/data/logs") / (std::string("ipsec_decap") + std::to_string(j) + ".txt");
                                                (void)append_file(decap_node.string(), decap_log.string());
                                        }
                                }
                        }
		}

		const bool has_debugfs = exists("/sys/kernel/debug/");
		// common on both debug/Normal
		std::vector<std::string> logs_common = {
			"/data/logs/ipa_stats.txt",
			"/data/logs/ipa_stats1.txt",
			"/data/logs/ipa_stats2.txt",
			"/data/logs/ipa_stats3.txt",
			"/data/logs/pm_stats.txt",
			"/data/logs/mhip_gsi_stats.txt",
			"/data/logs/odlstats.txt",
			"/data/logs/usb_gsi_stats.txt",
			"/data/logs/wdi_gsi_stats.txt",
			"/data/logs/wdi3_gsi_stats.txt",
			"/data/logs/dmesg.txt",
			"/data/logs/ipa_hdr.txt",
			"/data/logs/ipa_stats_final.txt",

			// Networking/eth stats logs
			"/data/logs/eth_qos_eth0.txt",
			"/data/logs/eth_qos_eth1.txt",
			"/data/logs/drop_stats.txt",
			"/data/logs/eth_iemac0.txt",
			"/data/logs/eth_iemac1.txt",
			"/data/logs/mobile_cfg.xml",
			"/data/logs/ipa_config.txt",

			// IPA
			"/data/logs/ipv6ct.txt",
			"/data/logs/ipa_ip4_rt.txt",
			"/data/logs/ipa_ip4_nat.txt",
			"/data/logs/ipa_ip4_flt.txt",
			"/data/logs/ipa_ip4_flt_hw.txt",
			"/data/logs/proc_ctx.txt",
			"/data/logs/ipa_status_stats1.txt",
			"/data/logs/ipa_status_stats2.txt",
			"/data/logs/ipa_ip6_rt.txt",
			"/data/logs/ipa_ip6_flt.txt",
			"/data/logs/ipa_ip6_flt_hw.txt",
			"/data/logs/ipsec_active_sa.txt"
		};

		for (int j = 0; j <= 10; ++j) {
                                logs_common.push_back(std::string("/data/logs/ipsec_encap") + std::to_string(j) + ".txt");
                                logs_common.push_back(std::string("/data/logs/ipsec_decap") + std::to_string(j) + ".txt");
                        }

		// Transfer logs only when /sys/kernel/debug/ exists
		std::vector<std::string> logs_debugfs = {
			"/data/logs/hw_stats.txt",
			"/data/logs/ipa_msg.txt"
		};

		// Transfer logs only when nodebugfs
		std::vector<std::string> logs_nodebugfs = {
			// IPA & related nodes
			"/data/logs/ntn.txt",
			"/data/logs/ipa_dscp_pcp_mapping_cache.txt",
			"/data/logs/aqc_0_err_status.txt",
			"/data/logs/enable_clock_scaling.txt",
			"/data/logs/ntn_perf_status.txt",
			"/data/logs/tx_wrapper_cache_max_size.txt",
			"/data/logs/rtk_0_err_status.txt",
			"/data/logs/page_poll_threshold.txt",
			"/data/logs/keep_awake.txt",
			"/data/logs/page_recycle_stats.txt",
			"/data/logs/clock_scaling_bw_threshold_turbo_mbps.txt",
			"/data/logs/clock_scaling_bw_threshold_nominal_mbps.txt",
			"/data/logs/lan_coal_stats.txt",
			"/data/logs/enable_napi_chain.txt",
			"/data/logs/page_wq_reschd_time.txt",
			"/data/logs/iemac_1_err_status.txt",
			"/data/logs/mpm_ring_size_dl.txt",
			"/data/logs/ipa_max_napi_sort_page_thrshld.txt",
			"/data/logs/ntn_1_err_status.txt",
			"/data/logs/mpm_ring_size_ul.txt",
			"/data/logs/ntn3_1_err_status.txt",
			"/data/logs/msg.txt",
			"/data/logs/mpm_teth_aggr_size.txt",
			"/data/logs/cache_recycle_stats.txt",
			"/data/logs/pm_ex_stats.txt",
			"/data/logs/ep_reg.txt",
			"/data/logs/aqc_1_err_status.txt",
			"/data/logs/hw_type.txt",
			"/data/logs/clk_rate.txt",
			"/data/logs/rtk_1_err_status.txt",
			"/data/logs/iemac_0_err_status.txt",
			"/data/logs/wdi.txt",
			"/data/logs/gen_reg.txt",
			"/data/logs/ntn_0_err_status.txt",
			"/data/logs/ntn3_0_err_status.txt",
			"/data/logs/wstats.txt",
			"/data/logs/mpm_uc_thresh.txt",
			"/data/logs/app_clk_vote_cnt.txt",
			"/data/logs/eth_status.txt",
			"/data/logs/hw_tethering_stats.txt",
			"/data/logs/gsi_fw_version.txt",
			"/data/logs/gsi_hw_profiling_stats.txt",
			"/data/logs/ipa_dump_regs.txt",
			"/data/logs/ipa_ip4_rt_hw.txt",
			"/data/logs/ipa_ip6_rt_hw.txt",
			"/data/logs/dmesg1.txt",
		};

		//Transfer: common files
		for (const auto& f : logs_common) {
			int rc = transfer_and_cleanup(f);
			if (rc != 0 && overall_rc == 0) overall_rc = rc;
		}

		//Transfer: Debug enabled files
		if (has_debugfs) {
			for (const auto& f : logs_debugfs) {
				int rc = transfer_and_cleanup(f);
				if (rc != 0 && overall_rc == 0) overall_rc = rc;
			}
		//Transfer: Non Debug files
		} else {
			for (const auto& f : logs_nodebugfs) {
				int rc = transfer_and_cleanup(f);
				if (rc != 0 && overall_rc == 0) overall_rc = rc;
			}
		}
	}

	// firmware
	if (firmware_enabled) {
		LOGI("firmware enabled - collecting");
		(void)system("sh -c 'if [ -f /firmware/verinfo/ver_info.txt ]; then "
				"cat /firmware/verinfo/ver_info.txt > /data/logs/ver_info.txt; "
				"elif [ -f /firmware/verinfo/Ver_Info.txt ]; then "
				"cat /firmware/verinfo/Ver_Info.txt > /data/logs/ver_info.txt; "
				"elif [ -f /firmware/image/Ver_Info.txt ]; then "
				"cat /firmware/image/Ver_Info.txt > /data/logs/ver_info.txt; "
				"else echo \"No matching file found.\" > /data/logs/ver_info.txt; fi'");
		(void)transfer_and_cleanup("/data/logs/ver_info.txt");
	}

	// If tcpdump enabled: stop tcpdump, wait, and transfer .pcap files into ipa_logs/
	if (tcpdump_enabled) {
		LOGI("Stopping tcpdump on all interfaces and transferring pcaps");
		for (const auto& kv : tcpdump_pids) {
			const int pid = kv.second;
			if (pid <= 0) continue;
			(void)system(std::string("sh -c 'kill -INT " + std::to_string(pid) + " 2>/dev/null || true'").c_str());
			(void)system(std::string("sh -c 'i=0; while [ $i -lt 30 ]; do kill -0 " +
						std::to_string(pid) + " 2>/dev/null || exit 0; i=$((i+1)); sleep 1; done'").c_str());
			(void)system(std::string("sh -c 'kill -TERM " + std::to_string(pid) + " 2>/dev/null || true'").c_str());
			(void)system(std::string("sh -c 'kill -KILL " + std::to_string(pid) + " 2>/dev/null || true'").c_str());
		}
		::sleep(5);

		for (const auto& kv : tcpdump_pids) {
			const std::string pcap = "/data/logs/" + kv.first + ".pcap";
			int rc = transfer_and_cleanup(pcap);
			if (rc != 0 && overall_rc == 0) overall_rc = rc;
		}
		for (const auto& pf : pid_files) { (void)::remove(pf.c_str()); }
	}

	// Post Clean /data/logs
	(void)clean_data_logs();

	return overall_rc;
}

int main(int argc, char *argv[])
{
	LOGI("Starting log-collector");

	std::ifstream ConfFile(CONF_FILE);
	if (!ConfFile.is_open()) {
		LOGE("Failed to Open : " << CONF_FILE);
		return -1;
	}

	Json::CharReaderBuilder builder;
	Json::String errs;
	if (!Json::parseFromStream(builder, ConfFile, &Conf, &errs)) {
		LOGE("Failed to parse JSON config: " << errs);
		return -1;
	}

	if (!Conf["log_transfer"]["enabled"].asBool()) {
		LOGI("Log transfer disabled in config.");
		return 0;
	}

	std::string baseRemotePath = Conf["log_transfer"]["transfer"]["remote_path"].asString();
	if (baseRemotePath.empty()) {
		LOGE("Config error: remote server path is missing ");
		return -1;
	}

	if (baseRemotePath.back() != '/') {
		baseRemotePath.push_back('/');
	}

	const std::string targetDir = append_timestamp_to_remotedir(baseRemotePath);

	// Connect TCP
	std::string serverAddr = Conf["server"]["address"].asString();
	int serverPort         = Conf["server"]["port"].asInt();
	Socket::ClientSocket clientSock(serverAddr, serverPort);
	int sock = clientSock.connectToServer();

	if (sock < 0) {
		LOGE("Socket Connection Error");
		return -1;
	}

	// Initialize & auth SSH
	SSH::SSH ssh;
	LIBSSH2_SESSION* session = ssh.initialize_session(sock);
	if (!session) {
		LOGE("SSH Session Init Failed");
		return -1;
	}
	if (!perform_authentication(ssh)) {
		LOGE("Authentication Failed");
		return -1;
	}

	int overall_rc = 0;

	// dmesg
	if (Conf["log_transfer"]["sources"]["dmesg"].asBool()) {
		int rc = handle_dmesg(ssh, targetDir);
		if (rc != 0) { LOGE("dmesg transfer failed"); overall_rc = rc; }
	}

	// logread
	if (Conf["log_transfer"]["sources"]["logread"].asBool()) {
		int rc = handle_logread(ssh, targetDir);
		if (rc != 0) { LOGE("logread transfer failed"); overall_rc = rc; }
	}

	// cnss_diag
	{
		const Json::Value& cnss = Conf["log_transfer"]["sources"]["cnss_diag"];
		const bool shouldRun = cnss.isObject() && cnss.get("enabled", false).asBool();
		if (shouldRun) {
			const int rc = handle_cnss_diag(ssh, targetDir);
			if (rc != 0 && overall_rc == 0) { LOGE("cnss_diag transfer failed"); overall_rc = rc; }
		}
	}

	// ipc_logs
	if (Conf["log_transfer"]["sources"]["ipc_logs"].asBool()) {
		int rc = handle_ipc_logging(ssh, targetDir);
		if (rc != 0) { LOGE("ipc_logs transfer failed"); overall_rc = rc; }
	}

	// IP_logs
	const Json::Value& ipaNode = Conf["log_transfer"]["sources"]["ipa_logging"];
	if (ipaNode.isObject() && ipaNode.get("enabled", false).asBool()) {
		int rc = handle_ipa_logging(ssh, targetDir);
		if (rc != 0) { LOGE("ipa_logging transfer failed"); overall_rc = rc; }
	}

	return overall_rc;
}
