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


using namespace std;

#define LOGD(message) (std::cout << "[collect-logs] D: " << message << std::endl)
#define LOGE(message) (std::cout << "[collect-logs] E: " << __func__ << "  " << message <<"  :  "<< std::strerror(errno) << std::endl)

#define CONF_FILE ("/etc/shutdown-logs.conf")
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

bool logExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool createBackupPath() {
    mode_t mode = 0755; // Permissions: rwx r-x r-x
    return (mkdir(BACKUP_DEST_DIR, mode) == 0);
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
     int sizeToWrite;
     z_stream strm;
     unsigned char in[ZLIB_CHUNK_SZ];
     unsigned char out[ZLIB_CHUNK_SZ];
     FILE *source = NULL;
     FILE *dest = NULL;

     // Open source file for reading as binary stream
     source = fopen(src.c_str(), "rb");
     if(NULL == source)
     {
         LOGE("failed to open source file " << src);
         return -1;
     }

     // Open destination file where the compressed
     // stream will be written
     dest = fopen(dst.c_str(), "wb");
     if(NULL == dest)
     {
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
     if( ret != Z_OK)
     {
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
             if( (ret = deflate(&strm, flush)) == Z_STREAM_ERROR)
             {
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

             if (
                 sizeToWrite < 0 ||
                 fwrite(
                 toWrite,
                 1,
                 (size_t)((unsigned)sizeToWrite),
                 dest) != (size_t)((unsigned)sizeToWrite) ||
                 ferror(dest))
             {
                 LOGE("dest file error " << std::strerror(errno));
                 (void)deflateEnd(&strm);
                 fclose(source);
                 fclose(dest);
                 return -1;
             }
         } while (strm.avail_out == 0);
         if(strm.avail_in != 0)
         {
             LOGE("zlib did not consume all of source");
             (void)deflateEnd(&strm);
             fclose(source);
             fclose(dest);
             return -1;
         }
         // done when when feof on source is true
     } while (flush != Z_FINISH);
     if(ret != Z_STREAM_END)
     {
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



int8_t removeFile(const string &file)
 {
     try
     {
         if(!logExists(file))
         {
             return 0;
         }

         // Remove tmp copy of file
         if(remove(file.c_str()) == 0)
         {
             LOGD("removed log file: "<< file);
         }
         else
         {
             LOGE("failed to remove file " << file);
             return -1;
         }
     }
     catch(const runtime_error& e)
     {
         LOGE("unable to remove file: " << e.what());
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
		    if(!createBackupPath())
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

    if (argc==2)
    {
        string conf_file = argv[1];
        LOGD("Collecting logs as mentioned in : "<<conf_file);
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
            if(line.empty())
                continue;
            if(line[0]=='#')
                continue;
            const string inputwords[2];
            string temp[2];
            int i = 0;
            while (iss >> temp[i] && i < 2) {
                i++;
            }

            if (i == 2) {
                const std::string inputwords[2] = {temp[0], temp[1]};
                if ( saveCompressedLogs(inputwords[0], dest_dir+inputwords[1]+"_"+ts+".gz") !=0)
                {
                    LOGE("Compression failed for :  "<< inputwords[0]);
                    removeFile(dest_dir+inputwords[1]+"_"+ts+".gz");
                }
                else
                {
                    LOGD(" Saved logs "<<inputwords[0] << "  at "<<dest_dir+inputwords[1]+"_"+ts+".gz");
                }
            } else {
                LOGE("Incorrect input line : " << line );
            }
        }

        inputFile.close();
    }
    else
    {
        //Save default logs - dmesg, journalctl
        for(int i=0;i<2;i++)
        {
            if ( executeCmdSaveLogs((dest_dir+commands[i]+".txt").c_str(),(commands[i]).c_str()) !=0)
            {
                LOGE("Failed to collect logs "<< commands[i]);
                removeFile(dest_dir+commands[i]+".txt");
            }
            else
            {
                if ( saveCompressedLogs(dest_dir+commands[i]+".txt", dest_dir+commands[i]+"_"+ts+".gz") !=0)
                    {
                        LOGE("Compression failed for :  "<< dest_dir+commands[i]+".txt");
                        removeFile(dest_dir+commands[i]+"_"+ts+".gz");
                    }
                    else
                    {
                        LOGD(" Saved logs " << "  at "<<dest_dir+commands[i]+"_"+ts+".gz");
                        removeFile(dest_dir+commands[i]+".txt");
                    }   
            }
        }
    }
	return ret;
}
