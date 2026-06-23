/* 
---------------------------------------------------------------------------
Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
---------------------------------------------------------------------------
*/
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "zlib.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstdio>  // For remove
#include <cstring> // For strerror
#include <cerrno>  // For errno
#include <stdexcept> //exception
#include <dirent.h>
#include <archive.h>
#include <archive_entry.h>
#include <vector>
#include <filesystem>


using namespace std;
namespace fs = std::filesystem;

#define LOGD(message) (std::cout << "[collect-logs] D: " << message << std::endl)
#define LOGE(message) (std::cout << "[collect-logs] E: " << __func__ << "  " << message <<"  :  "<< std::strerror(errno) << std::endl)

#define DEST_DIR ("/data/mplane/o-ran-filesystem/o-ran/log/")
/*#define DEST_DIR ("/data/")*/
#define BACKUP_DEST_DIR ("/data/shutdown-logs/")
#define ZLIB_CHUNK_SZ 16384
#define GZIP_COMPRESSION_LVL 1
string commands[2]={"dmesg", "journalctl"};

//Check if DEST_DIR path is available for log saving
bool destPathAvailable(const std::string& path)
{
	struct stat info;
	return (stat(path.c_str(),&info) == 0 && (info.st_mode & S_IFDIR));
}

bool createDir(const char* dir) {
    mode_t mode = 0755; // Permissions: rwx r-x r-x
    return (mkdir(dir, mode) == 0);
}

const char* zlibErrorCodeStr(int ret_code)
{
    switch (ret_code)
    {
    case Z_BUF_ERROR:
        return "No progress is possible; either avail_in or avail_out was zero";
    case Z_STREAM_ERROR:
        return "stream state was inconsistent";
    case Z_DATA_ERROR:
        return "invalid or incomplete deflate data.";
    case Z_MEM_ERROR:
        return "error, out of memory.";
    case Z_VERSION_ERROR:
        return "zlib version mismatch!";
    }
    return "unknown error code from zlib!";
}

int8_t saveCompressedLogs(const string &src, const string &dst)
 {
    int ret, flush;
    unsigned have;
    size_t sizeToWrite;
    z_stream strm;
    unsigned char in[ZLIB_CHUNK_SZ];
    unsigned char out[ZLIB_CHUNK_SZ];
    FILE *source = NULL;
    FILE *dest = NULL;

    // Open source file for reading as binary stream
    source = fopen(src.c_str(), "rb");
    if(NULL == source) {
        LOGE("failed to open source file " << src);
        return -1;
    }

    // Open destination file where the compressed
    // stream will be written
    dest = fopen(dst.c_str(), "wb");
    if(NULL == dest) {
        LOGE("failed to open destination file: " << dst);
        fclose(source);
        return -1;
    }

    // zlib stream buffers
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // deflateInit2 for .gz file header support
    // must set bits 15|16 for proper .gz header
    ret = deflateInit2(&strm, GZIP_COMPRESSION_LVL,
        Z_DEFLATED,15|16, 8, Z_DEFAULT_STRATEGY);
    if( ret != Z_OK) {
        LOGE("compression failed : " << zlibErrorCodeStr(ret));
        (void)deflateEnd(&strm);
        fclose(source);
        fclose(dest);
        return -1;
    }

    // read complete source file
    do
    {
        strm.avail_in = fread(in, 1, ZLIB_CHUNK_SZ, source);
        if (ferror(source))
        {
            LOGE("source file error " << std::strerror(errno));
            (void)deflateEnd(&strm);
            fclose(source);
            fclose(dest);
            return -1;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        // continue to compress until output buffer is full.
        // if source stream is all read in then finish
        do
        {
            strm.avail_out = ZLIB_CHUNK_SZ;

            strm.next_out = out;
            if( (ret = deflate(&strm, flush)) == Z_STREAM_ERROR) {
                LOGE("deflate error " << zlibErrorCodeStr(ret));
                (void)deflateEnd(&strm);
                fclose(source);
                fclose(dest);
                return -1;
            }
            have = ZLIB_CHUNK_SZ - strm.avail_out;

            unsigned char* toWrite;

            toWrite = out;
            sizeToWrite = have;

            if (fwrite(toWrite,1,sizeToWrite,dest) != sizeToWrite || ferror(dest)) {
                LOGE("dest file error " << std::strerror(errno));
                (void)deflateEnd(&strm);
                fclose(source);
                fclose(dest);
                return -1;
            }
        } while (strm.avail_out == 0);

        if(strm.avail_in != 0) {
            LOGE("zlib did not consume all of source");
            (void)deflateEnd(&strm);
            fclose(source);
            fclose(dest);
            return -1;
        }
        // done when when feof on source is true
    } while (flush != Z_FINISH);

    if(ret != Z_STREAM_END) {
        LOGE("zlib did not complete..");
        (void)deflateEnd(&strm);
        fclose(source);
        fclose(dest);
        return -1;
    }

    // Cleanup and return success
    (void)deflateEnd(&strm); // Free ZLIB memory
    fclose(source);          // Close file handle
    fclose(dest);            // Write any Buffer data and Close file handle

    return 0;
}

int SaveCompressedFolder(const string& folder_path, const string& zip_path) {
    struct archive* archive = archive_write_new();
    if (!archive) {
        LOGE("Failed to create archive object");
        return 1;
    }

    if (archive_write_set_format_zip(archive) != ARCHIVE_OK) {
        LOGE("Failed to set ZIP format");
        archive_write_free(archive);
        return 2;
    }

    if (archive_write_open_filename(archive, zip_path.c_str()) != ARCHIVE_OK) {
        LOGE("Failed to open ZIP file");
        archive_write_free(archive);
        return 3;
    }

    for (const auto& entry : fs::directory_iterator(folder_path)) {

        ifstream file(entry.path(), ios::binary);
        if (!file) {
            LOGE("Failed to open file: " << entry.path());
            continue;
        }

        vector<char> buffer((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

        struct archive_entry* archive_entry = archive_entry_new();

        archive_entry_set_pathname(archive_entry, entry.path().filename().string().c_str());
        archive_entry_set_size(archive_entry, buffer.size());
        archive_entry_set_filetype(archive_entry, AE_IFREG);
        archive_entry_set_perm(archive_entry, 0644);

        if (archive_write_header(archive, archive_entry) != ARCHIVE_OK) {
            LOGE("Failed to write header for: " << entry.path());
            archive_entry_free(archive_entry);
            continue;
        }

        if (archive_write_data(archive, buffer.data(), buffer.size()) < 0) {
            LOGE("Failed to write data for: " << entry.path());
            archive_entry_free(archive_entry);
            continue;
        }

        archive_entry_free(archive_entry);
    }

    if (archive_write_close(archive) != ARCHIVE_OK) {
        LOGE("Failed to close archive");
        archive_write_free(archive);
        return 4;
    }

    archive_write_free(archive);
    return 0;
}

int8_t removeFile(const string &path)
 {
    struct stat buffer;
    if (lstat(path.c_str(), &buffer) != 0) {
        return 0;
    }

    if (S_ISDIR(buffer.st_mode)) {

        DIR* dir = opendir(path.c_str());
        if (!dir) {
            LOGE("opendir failed: " << path);
            return -1;
        }

        struct dirent* ent;
        bool innerFailed = false;
        while ((ent = readdir(dir)) != nullptr) {
            const char* name = ent->d_name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }

            string file = path + '/' + name;

            if(remove(file.c_str()) == 0) {
                LOGD("removed compressed log file: " << file);
            }
            else {
                LOGE("failed to remove file " << file);
                innerFailed = true;  // track failure
            }
        }
        closedir(dir);
        if (innerFailed) return -1;  // don't attempt rmdir if contents remain

    }
    // Remove tmp copy of file
    if(remove(path.c_str()) == 0) {
        LOGD("Removed : "<< path);
    }
    else {
        LOGE("Failed to remove:  " << path);
        return -1;
    }
    return 0;
}

int8_t executeCmdSaveLogs(const char *filename, const char *cmd) {
    FILE *fp;
    char buffer[512];

    // Open the file to save the logs
    FILE *logFile = fopen(filename, "w");
    if (logFile == NULL) {
        LOGE("Failed to open file "<< filename);
        return -1;
    }

    // Execute the command and open a pipe to read its output
    fp = popen(cmd, "r");
    if (fp == NULL) {
        LOGE("Failed to run dmesg command: "<< cmd);
        fclose(logFile);
        return -1;
    }

    // Read the output of the command and write it to the file
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        fputs(buffer, logFile);
    }

    // Close the pipe and the file
    pclose(fp);
    fclose(logFile);
    return 0;
}


int main(int argc , char* argv[])
{
	int ret=0; // success
	string dest_dir = DEST_DIR;
	/*
	if DEST_DIR exists, save logs to DEST_DIR,
	if DEST_DIR doesnt exists, create BACKUP_DEST_DIR and save logs there
	*/
	if (!destPathAvailable(dest_dir))
	{
		LOGD(" mplane path " << DEST_DIR<< " is not available, logs will be saved to backup path : " << BACKUP_DEST_DIR);
        dest_dir = BACKUP_DEST_DIR;
        if (!destPathAvailable(dest_dir))
        {
            LOGD(" Creating backup path now: "<< BACKUP_DEST_DIR);
		    if(!createDir(BACKUP_DEST_DIR))
		    {
			    LOGE("Unable to create log path: "<< BACKUP_DEST_DIR << " . Logs will not be saved !! .");
			    return -1;
		    }
        }
	}

	//get current timestamp to be appended to all logs while saving
	auto now = chrono::system_clock::now();
	time_t now_c = chrono::system_clock::to_time_t(now);
	tm* now_tm = localtime(&now_c);
	ostringstream oss;

	if (now_tm == nullptr)
	{
		LOGE("Locatime returned null. Saving logs without timestamp.");
		oss << "00";
	}
	else
	{
		tm now_tm_copy = *now_tm;
		oss << put_time(&now_tm_copy, "%y%m%d%H%M%S");
	}

	string ts = oss.str();
	LOGD("Saving shutdown logs with timestamp: "<<ts);

    dest_dir += ts;

    if (!createDir(dest_dir.c_str())) {
		LOGE("Unable to create log path: "<< dest_dir << " . Logs will not be saved !! .");
		return -1;
	}

    if (argc==2) {
        string conf_file = argv[1];
        LOGD("Collecting logs as mentioned in : " << conf_file);
        //Open shutdown-logs.conf, parse it line by line and save logs using saveCompressedLogs API
        ifstream inputFile(conf_file);
        if (!inputFile.is_open()) {
            LOGE("Error opening file!" <<  conf_file);
            return -1;
        }

        string line;
        while (getline(inputFile, line)) {
            /*parse input log path and o/p file name
            skip if line starts with #
            skip if empty line
            */
            istringstream iss(line);

            if (line.empty()) continue;

            if (line[0]=='#') continue;

            string inputwords[2];
            int i = 0;
            while (i < 2 && iss >> inputwords[i]) {
                i++;
            }

            struct stat srcStat;

            if (stat(inputwords[0].c_str(), &srcStat) != 0) {
                LOGD("Source path does not exist: " << inputwords[0]);
                continue;
            }

            if ( i == 2 && S_ISREG(srcStat.st_mode)) {
                // Source is a regular file — compress as .gz
                const string gzDst = dest_dir + '/' + inputwords[1] + ".gz";
                if (saveCompressedLogs(inputwords[0], gzDst) != 0) {
                    LOGE("Compression failed for :  " << inputwords[0]);
                    removeFile(gzDst);
                } else {
                    LOGD(" Saved logs "<<inputwords[0] << "  at " << gzDst);
                }
            }
            else if (S_ISDIR(srcStat.st_mode)) {
            // Source is a directory — compress each file inside it individually as .gz
                for (const auto& dirEntry : fs::directory_iterator(inputwords[0])) {
                    if (!dirEntry.is_regular_file()) {
                            LOGD("Skipping non-regular file: " << dirEntry.path());
                            continue;
                    }
                    const string fileName  = dirEntry.path().filename().string();
                    const string gzDst     = dest_dir + '/' + fileName + ".gz";
                    if (saveCompressedLogs(dirEntry.path().string(), gzDst) != 0) {
                        LOGE("Compression failed for: " << dirEntry.path());
                        removeFile(gzDst);
                    } else {
                        LOGD(" Saved logs " << dirEntry.path() << "  at " << gzDst);
                    }
                }
            } else {
                LOGE("Incorrect input line : " << line );
            }
        }

        inputFile.close();
    } else {
        //Save default logs - dmesg, journalctl
        for (int i=0 ; i<2 ; i++) {
            const string log_file = dest_dir + '/' + commands[i] + ".txt";
            const string dst_file = dest_dir + '/' + commands[i] + ".gz";
            if ( executeCmdSaveLogs(log_file.c_str(),(commands[i]).c_str()) !=0) {
                LOGE("Failed to collect logs "<< commands[i]);
                removeFile(log_file);
            } else {
                if ( saveCompressedLogs(log_file, dst_file) !=0) {
                    LOGE("Compression failed for :  " << log_file);
                    removeFile(dst_file);
                } else {
                    LOGD(" Saved logs " << "  at " << dst_file);
                    removeFile(log_file);
                }   
            }
        }
    }

    if ( SaveCompressedFolder(dest_dir, dest_dir + ".zip") !=0)
    {
        LOGE("Compression failed for :  " << dest_dir);
        removeFile(dest_dir+".zip");
    }
    else
    {
        LOGD(" Saved logs " << "  at " << dest_dir + ".zip");
        removeFile(dest_dir);
    }
	return ret;
}
