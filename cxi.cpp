// $Id: cxi.cpp,v 1.5 2021-05-18 01:32:29-07 - - $
// Emmanuel Pena - epena6 
// Kidus Michael - kimichae

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <cstring>
#include <sstream>

using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "logstream.h"
#include "protocol.h"
#include "socket.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"get"  , cxi_command::GET},
   {"put"  , cxi_command::PUT},
   {"rm"  , cxi_command::RM  },
};

using wordvec = vector<string>;
wordvec split (const string& line, const string& delimiters)
{
   wordvec words;
   size_t end = 0;

   for (;;) 
   {
      size_t start = line.find_first_not_of (delimiters, end);
      if (start == string::npos) break;
         end = line.find_first_of (delimiters, start);
         words.push_back (line.substr (start, end - start));
   }

   return words;
}


static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   DEBUGF ('h', "sending header " << header << endl);
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   DEBUGF ('h', "received header " << header << endl);
   if (header.command != cxi_command::LSOUT) {
      outlog << "Sent LS, server did not return LSOUT" << endl;
      outlog << "Server returned " << header << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      outlog << "Received " << host_nbytes << " Bytes" << endl;
      DEBUGF ('h', "received " << host_nbytes << " bytes");
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}


void cxi_get(client_socket& server, string& file) {
   cxi_header header;
   header.command = cxi_command::GET;

   strcpy(header.filename, file.c_str());

   outlog << "Sending Header (cxi)" << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "Recieved Header (cxi)" << endl;

   if (header.command != cxi_command::FILEOUT) {
      errno = ENOENT;
      outlog << "Error with cxi_Get. Error: " 
             << strerror(errno)
             << endl;
   }
   else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
    
      buffer[host_nbytes] = '\0';

      ofstream fileee;
      fileee.open(file);
      fileee << buffer.get();
      fileee.close();

      outlog << "cxi: get successful for "
             << file 
             << endl;
   }
}

void cxi_put(client_socket& server, string& file) {
   cxi_header header; 
   header.command = cxi_command::PUT;
   strcpy(header.filename, file.c_str());

   ifstream fileeee (header.filename, ifstream::binary);

   if(!fileeee) {
      header.command = cxi_command::NAK;
      errno = ENOENT;
      header.nbytes = htonl (errno);
      send_packet (server, &header, sizeof header);
   }
   else {
      fileeee.seekg(0, fileeee.end);
      int length = fileeee.tellg();
      fileeee.seekg(0, fileeee.beg);

      header.nbytes = htonl (length);
      char buffer[0x1000];

      fileeee.read(buffer, length);
      fileeee.close();

      outlog << "Sending Header (cxi)" << endl;
      send_packet (server, &header, sizeof header);
      outlog << "Sending Body (cxi)" << endl;
      send_packet (server, buffer, length);
      recv_packet (server, &header, sizeof header);
      outlog << "Recieved Header (cxi)" << endl;
      memset (header.filename, 0, FILENAME_SIZE);
   }
   
   if(header.command == cxi_command::ACK)
   {
     outlog << "Cix_put " << file << " done"
             << endl; 
   }
   else if(header.command == cxi_command::NAK)
   {
      errno = ENOENT;
      outlog << "Error with Cix_put. Error: "
             << strerror (errno)
             << endl;
   }
   else if(header.command != cxi_command::ACK)
   {
      errno = ENOENT;
      outlog << "Error with Cix_put. Error: " 
             << strerror (errno) 
             << endl;
   }
   else 
   {
      outlog << "Cix_put " << file << " done"
             << endl;
   }
}   


void cxi_rm(client_socket& server, string filename)
{
   cxi_header header;
   header.command = cxi_command::RM;
   
   strcpy (header.filename, filename.c_str());
   outlog << "Sending header (cxi)" << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "Received header (cxi)" << endl;   

   if (header.command == cxi_command::ACK) 
   {
      outlog << "Succesfully removed file \"" << header.filename 
             << "\"" << endl;
   }
   else if (header.command == cxi_command::NAK)
   {  
      outlog << "Error removing file \"" << header.filename 
             << "\"" << endl;
   }
}


void usage() {
   cerr << "Usage: " << outlog.execname() << " host port" << endl;
   throw cxi_exit();
}

pair<string,in_port_t> scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 2) usage();
   string host = argv[optind];
   in_port_t port = get_cxi_server_port (argv[optind + 1]);
   return {host, port};
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << to_string (hostinfo()) << endl;
   try {
      auto host_port = scan_options (argc, argv);
      string host = host_port.first;
      in_port_t port = host_port.second;
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         wordvec words = split(line, " \t");
         if (words.size() == 0)
         {
            continue;
         }
         outlog << "command " << line << endl;
         const auto& itor = command_map.find (words[0]);
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         string filename;
         if(cmd == cxi_command::PUT || cmd == cxi_command::GET
            || cmd == cxi_command::RM)
       {
            if (words.size() < 2)
           {
               outlog << words[0] << ":requires filename" << endl;
               continue;
            }
            if (words.size() > 2)
            {
               outlog << words[0] << ": too many arguments.. " 
                      << "will use first argument" << endl;
               continue;
            }
            filename = words[1];
         }
            switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            case cxi_command::PUT:
               cxi_put (server, filename);
               break;
            case cxi_command::GET:
               cxi_get (server, filename);
               break;
            case cxi_command::RM:
               cxi_rm (server, filename);
               break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}

