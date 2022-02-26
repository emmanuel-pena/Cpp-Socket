// $Id: cxid.cpp,v 1.8 2021-05-18 01:32:29-07 - - $
// Emmanuel Pena - epena6
// Kidus Michael - kimichae

#include <iostream>
#include <string>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <memory>

#include "debug.h"
#include "logstream.h"
#include "protocol.h"
#include "socket.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

void reply_ls (accepted_socket& client_sock, cxi_header& header) {
   static const char ls_cmd[] = "ls -l 2>&1";
   FILE* ls_pipe = popen (ls_cmd, "r");
   if (ls_pipe == nullptr) { 
      outlog << ls_cmd << ": " << strerror (errno) << endl;
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
   }
   string ls_output;
   char buffer[0x1000];
   for (;;) {
      char* rc = fgets (buffer, sizeof buffer, ls_pipe);
      if (rc == nullptr) break;
      ls_output.append (buffer);
   }
   pclose (ls_pipe);
   header.command = cxi_command::LSOUT;
   header.nbytes = htonl (ls_output.size());
   memset (header.filename, 0, FILENAME_SIZE);
   DEBUGF ('h', "sending header " << header);
   send_packet (client_sock, &header, sizeof header);
   send_packet (client_sock, ls_output.c_str(), ls_output.size());
   DEBUGF ('h', "sent " << ls_output.size() << " bytes");
}

void reply_get(accepted_socket& client_sock, cxi_header& header) {
   ifstream file (header.filename, ifstream::binary);

   if(!file) 
   {
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
   }

   //get length of data
   file.seekg(0, file.end);
   int length = file.tellg();
   file.seekg(0, file.beg);

   //create a buffer
   char buffer[0x1000];

   //read into the buffer the length of data
   file.read(buffer, length);
   file.close();
   header.command = cxi_command::FILEOUT;
   header.nbytes = htonl (length);

   memset (header.filename, 0, FILENAME_SIZE);
   outlog << "Sending Header" << endl;
   send_packet (client_sock, &header, sizeof header);
   outlog << "Sending Body" << endl;
   send_packet (client_sock, buffer, length);
}

void reply_put(accepted_socket& client_sock, cxi_header& header)
{
   header.command = cxi_command::ACK;
   size_t bytess = ntohl (header.nbytes);
   auto buffer = make_unique<char[]> (bytess + 1);
   recv_packet (client_sock, buffer.get(), bytess);
   outlog << "Recieved " << bytess << " Bytes" << endl;
   buffer[bytess] = '\0';

   ofstream filee;
   filee.open(header.filename);
   filee << buffer.get();
   filee.close();
   outlog << "Sending Header" << endl;
   send_packet (client_sock, &header, sizeof header);
}

void reply_rm (accepted_socket& client_sock, cxi_header& header)
{

   int rm = unlink(header.filename);

   if (rm == 0)  //success
   {
      outlog << "Removed " << header.filename << endl;
      header.command = cxi_command::ACK;  //acknowledged
      header.nbytes = htonl(0);
      outlog << "Sending header " << endl;
      send_packet (client_sock, &header, sizeof header);
   } 
   else //failed
   {
      outlog << "Error removing " << header.filename << " : Error: " 
             << strerror (errno) << endl;
      header.command = cxi_command::NAK;  //negative acknowledged
      header.nbytes = errno;
      outlog << "Sending header " << endl;
      send_packet (client_sock, &header, sizeof header);
   }
}

void run_server (accepted_socket& client_sock) {
   outlog.execname (outlog.execname() + "*");
   outlog << "connected to " << to_string (client_sock) << endl;
   try {   
      for (;;) {
         cxi_header header; 
         recv_packet (client_sock, &header, sizeof header);
         DEBUGF ('h', "received header " << header);
         switch (header.command) {
            case cxi_command::LS: 
               reply_ls(client_sock, header);
               break; 
            case cxi_command::GET: 
               reply_get(client_sock, header);
               break;
            case cxi_command::PUT:
               reply_put(client_sock, header);
               break;
            case cxi_command::RM: 
               reply_rm(client_sock, header);
               break;
            default:
               outlog << "invalid client header:" << header << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   throw cxi_exit();
}

void fork_cxiserver (server_socket& server, accepted_socket& accept) {
   pid_t pid = fork();
   if (pid == 0) { // child
      server.close();
      run_server (accept);
      throw cxi_exit();
   }else {
      accept.close();
      if (pid < 0) {
         outlog << "fork failed: " << strerror (errno) << endl;
      }else {
         outlog << "forked cxiserver pid " << pid << endl;
      }
   }
}


void reap_zombies() {
   for (;;) {
      int status;
      pid_t child = waitpid (-1, &status, WNOHANG);
      if (child <= 0) break;
      if (status != 0) {
         outlog << "child " << child
                << " exit " << (status >> 8)
                << " signal " << (status & 0x7F)
                << " core " << (status >> 7 & 1) << endl;
      }
   }
}

void signal_handler (int signal) {
   DEBUGF ('s', "signal_handler: caught " << strsignal (signal));
   reap_zombies();
}

void signal_action (int signal, void (*handler) (int)) {
   struct sigaction action;
   action.sa_handler = handler;
   sigfillset (&action.sa_mask);
   action.sa_flags = 0;
   int rc = sigaction (signal, &action, nullptr);
   if (rc < 0) outlog << "sigaction " << strsignal (signal)
                      << " failed: " << strerror (errno) << endl;
}



void usage() {
   cerr << "Usage: " << outlog.execname() << " port" << endl;
   throw cxi_exit();
}

in_port_t scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 1) usage();
   return get_cxi_server_port (argv[optind]);
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   signal_action (SIGCHLD, signal_handler);
   try {
      in_port_t port = scan_options (argc, argv);
      server_socket listener (port);
      for (;;) {
         outlog << to_string (hostinfo()) << " accepting port "
             << to_string (port) << endl;
         accepted_socket client_sock;
         for (;;) {
            try {
               listener.accept (client_sock);
               break;
            }catch (socket_sys_error& error) {
               switch (error.sys_errno) {
                  case EINTR:
                     outlog << "listener.accept caught "
                         << strerror (EINTR) << endl;
                     break;
                  default:
                     throw;
               }
            }
         }
         outlog << "accepted " << to_string (client_sock) << endl;
         try {
            fork_cxiserver (listener, client_sock);
            reap_zombies();
         }catch (socket_error& error) {
            outlog << error.what() << endl;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}
