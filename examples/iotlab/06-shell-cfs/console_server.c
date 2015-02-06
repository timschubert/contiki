#include "contiki.h"
#include "dev/serial-line.h"
#include <stdio.h>
#include <string.h>

#include "cfs/cfs-coffee.h"


PROCESS(console_server, "Console_server");
AUTOSTART_PROCESSES(&console_server);

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(console_server, ev, data)
{
  static uint8_t buf[257];
  static uint8_t processingFile = 0;
  static uint32_t received = 0;
  static struct cfs_dirent dirent;
  static struct cfs_dir dir;
  static uint32_t fdFile;
  PROCESS_BEGIN();

  printf("Console server started !\n");
  while(1) {

    PROCESS_YIELD();
    if (ev == serial_line_event_message) {
      if (!strcmp(data, "ls")) {
        if(cfs_opendir(&dir, ".") == 0) {
          while(cfs_readdir(&dir, &dirent) != -1) {
            printf("File: %s (%ld bytes)\n",
                dirent.name, (long)dirent.size);
          }
          cfs_closedir(&dir);
        }
      }
      else if (!strcmp(data, "format")) {
        /* format the flash */
        printf("Formatting\n");
        printf("It takes around 3 minutes\n");
        printf("...\n");

        fdFile = cfs_coffee_format();
        printf("Formatted with result %ld\n", fdFile);
      }
      else if (strstr(data, "cat") == data) {
        int n, jj;
        char* tmp = strstr(data, " ");
        tmp++;
        fdFile = cfs_open(tmp, CFS_READ);
        if (fdFile < 0) printf("error opening the file %s\n", tmp);
        while ((n = cfs_read(fdFile, buf, 60)) > 0) {
          for (jj = 0 ; jj < n ; jj++) printf("%c", (char)buf[jj]);
          /*fflush(stdout);*/
        }
        printf("\n");
        cfs_close(fdFile);
        if (n!=0)
          printf("Some error reading the file\n");
      }
      else if (strstr(data, "rm") == data) {
        int n, jj;
        char* tmp = strstr(data, " ");
        tmp++;
        cfs_remove(tmp);
      }
      else if (strstr(data, "upload") == data) {
        char* tmp = strstr(data, " ");
        tmp++;
        fdFile = cfs_open(tmp, CFS_READ | CFS_WRITE);
        printf("Uploading file %s\n", tmp);
        processingFile = 1;
      }
      else if (!strcmp(data, "endupload")) {
        cfs_close(fdFile);
        printf("File uploaded (%ld bytes)\n", received);
        received = 0;
        processingFile = 0;
      }
      else if (processingFile) {
        int n = strlen(data);
        int r = decode(data, n, buf);
        received += r;
        //printf("%s", (char*)d);
        cfs_write(fdFile, buf, r);
      }
      else  {
        printf("%s (%lu bytes received)\n", (char*)data, received);
      }
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
