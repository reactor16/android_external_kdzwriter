/* **********************************************************************
* Copyright (C) 2017 Elliott Mitchell					*
*									*
*	This program is free software: you can redistribute it and/or	*
*	modify it under the terms of the GNU General Public License as	*
*	published by the Free Software Foundation, either version 3 of	*
*	the License, or (at your option) any later version.		*
*									*
*	This program is distributed in the hope that it will be useful,	*
*	but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	*
*	GNU General Public License for more details.			*
*									*
*	You should have received a copy of the GNU General Public	*
*	License along with this program.  If not, see			*
*	<http://www.gnu.org/licenses/>.					*
*************************************************************************
*$Id$			*
************************************************************************/


#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include "bootimg.h"


struct misc_hwinfo {
	char magic[16];  // must be set to "LGE ONE BINARY" to be valid.
	uint32_t reserved;
	uint16_t hwinfo_major;
	uint16_t hwinfo_minor;
	char lg_model_name[20];
	uint32_t sim_num;
};

const char magic[]="LGE ONE BINARY\0";


char model_name[23]="";
char sim_str[2]={'\0', '\0'};
#ifndef LINEAGEOS
char dsds_str[10]; //info->sim_num==2?"dsds":"none"
#endif
char cmdline[2048]; /* lots of slack so all the entries can be appended */


struct entry {
	const char *const key;
	const char *const val;
	const unsigned overwrite:1;
};

const struct entry entries[]={
	{" model.name=",			model_name,	1},
	{" lge.sim_num=",			sim_str,	1},
#ifndef LINEAGEOS
	{" lge.dsds=",				dsds_str,	1},
	{" androidboot.bl_unlock_complete=",	"false",	0},
	{" androidboot.authorized_kernel=",	"true",		0},
#endif
};


/* come on Android, even the Linux kernel has strchrnul() */
static char *strchrnul(const char *s, int c)
{
	char *r;
	return (r=strchr(s, c))?r:(char *)(s+strlen(s));
}

int main(int argc, char **argv)
{
	int fd;
	uint32_t blksz;
	char *buf;
	boot_img_hdr *bootimg;
	uint8_t sim_num=0;
	int i;
	int opt;
	int verbose=1;


	while((opt=getopt(argc, argv, "1234m:qvhH?"))>=0) {
		switch(opt) {
		case '1':
		case '2':
		case '3':
		case '4':
			sim_num=opt-'0';
			break;
		case 'm':
			if(verbose&&memcmp("LG-", optarg, 3))
				fprintf(stderr,
"Warning: Provided model name doesn't start with \"LG-\", this is abnormal!\n");
			strlcpy(model_name, optarg, sizeof(model_name));
			break;

		case 'q':
			verbose=0;
			break;
		case 'v':
			verbose=2;
			break;

		case 'h':
		case 'H':
		case '?':
		default:
			fprintf(stderr,
"Usage: %s [-m <model>] [-1|-2] [-q] [-v] [-h] [<Android boot image>]\n"
"  -m <model>  Specify model name which generally starts with \"LG-H\".  Known\n"
"      strings include \"LG-H990\", \"LG-H990ds\", \"LG-H990N\" and \
\"LG-H990TR\"\n"
"  -1  Specify model is single-SIM\n"
"  -2  Specify model is dual-SIM\n"
"  -q  Quiet mode (no output)\n"
"  -v  Verbose mode\n"
"If the model isn't specified on command-line, an attempt will be made to\n"
"retrieve the model name from the \"misc\" area.  References to a LG-H990V and\n"
"LG-H998 have been found, but no hardware has been seen yet.\n", argv[0]);
			return 1;
		}
	}

	if((argc-optind)>1) {
		fprintf(stderr, "Too many non-option arguments, only 1 boot image allowed.\n");
		return 1;
	}


	if((fd=open("/dev/block/bootdevice/by-name/misc", O_RDONLY|O_LARGEFILE))<0) {
		fprintf(stderr, "Failed to open misc area: %s\n",
strerror(errno));
		return 1;
	}

	/* I hope no one produces UFS with multiple block sizes */
	if(ioctl(fd, BLKSSZGET, &blksz)<0) {
		fprintf(stderr, "Failed to get device block size: %s\n",
strerror(errno));
		return 1;
	}

	/* our minimum standard */
	if(blksz<2048) blksz=2048;

	if(!(buf=malloc(blksz))) {
		fprintf(stderr, "Memory allocation failed.\n");
		return 1;
	}


	if(!model_name[0]||verbose) {
		const char *error;
		const struct misc_hwinfo *info;

		if(lseek(fd, blksz*103, SEEK_SET)<0) {
			error="Seek failed: %s\n";
			goto problem;
		}

		if(read(fd, buf, blksz)!=blksz) {
			error="Read failed: %s\n";
			goto problem;
		}

		info=(struct misc_hwinfo *)buf;

		if(memcmp(info->magic, magic, sizeof(magic))) {
			error="Hardware information missing from \"misc\" area!\n";
			goto problem;
		}

		if(verbose) printf("LGE model information format version %hd.%hd\n", info->hwinfo_major, info->hwinfo_minor);

		if(!model_name[0]||!strcmp(model_name, info->lg_model_name))
			strlcpy(model_name, info->lg_model_name,
sizeof(model_name));
		else if(verbose)
			fprintf(stderr,
"Warning model name differs from misc area (\"%s\").\n\a\n",
info->lg_model_name);

		while(0) {
		problem:
			if(!model_name[0]||verbose) fprintf(stderr, error,
strerror(errno));
			if(!model_name[0]) {
				close(fd);
				free(buf);
				return 1;
			}
		}
	}
	close(fd);


	if(!sim_num) sim_num=!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1;
	else if(verbose&&(!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1)!=sim_num)
		fprintf(stderr,
"Alert!  Specified SIM count differs from expected count (%d).\n\a\n",
!strcmp(model_name, "LG-H990ds")||!strcmp(model_name, "LG-H990N")?2:1);


	sim_str[0]='0'+sim_num;

#ifndef LINEAGEOS
	strcpy(dsds_str, sim_num==1?"none":"dsds");
#endif


	if(argc-optind==0) {
		if(verbose) {
			fputs("Options to add/override in boot image: \"",
stdout);
			for(i=0; i<sizeof(entries)/sizeof(entries[0]); ++i) {
				fputs(entries[i].key+!i, stdout);
				fputs(entries[i].val, stdout);
			}
			fputs("\"\n", stdout);
		}

		return 0;
	}



	if(!strcmp(argv[optind], "-")) fd=0;
	else if((fd=open(argv[optind], O_RDWR|O_LARGEFILE))<0) {
		if(verbose) fprintf(stderr, "Failed when opening \"%s\": %s\n",
argv[optind], strerror(errno));
		close(fd);
		return 1;
	}


	if(read(fd, buf, blksz)!=blksz) {
		fprintf(stderr, "Read failed: %s\n", strerror(errno));
		return 1;
	}

	bootimg=(boot_img_hdr *)buf;


	memcpy(cmdline, bootimg->cmdline, BOOT_ARGS_SIZE);
	memcpy(cmdline+BOOT_ARGS_SIZE, bootimg->extra_cmdline, BOOT_EXTRA_ARGS_SIZE);
	cmdline[BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE]='\0';

	for(i=0; i<sizeof(entries)/sizeof(entries[0]); ++i) {
		char *key, *val;
		char *end;

		if((key=strstr(cmdline, entries[i].key)))
			val=key+strlen(entries[i].key);

		else if(!strncmp(cmdline, entries[i].key+1, strlen(entries[i].key)-1))
			val=cmdline+strlen(entries[i].key)-1;

		else {
			/* cmdline has more than enough slack for this */
			strcat(cmdline, entries[i].key);
			strcat(cmdline, entries[i].val);

			continue; /* done for this one */
		}

		/* some of the values can safely be left alone */
		if(!entries[i].overwrite) continue;

		if(!strncmp(val, entries[i].val, strlen(entries[i].val))) continue;

		/* sigh, need to do something about this */
		end=strchrnul(val, ' ');
		if(end-val!=strlen(entries[i].val)) /* different length */
			memmove(val+strlen(entries[i].val), end, strlen(end)+1);

		memcpy(val, entries[i].val, strlen(entries[i].val));
	}

	i=strlen(cmdline);
	memset(cmdline+i, 0, sizeof(cmdline)-i);

	if(verbose) fprintf(stderr, "New boot image command line is: \"%s\"\n", cmdline);

	if(i>BOOT_ARGS_SIZE+BOOT_EXTRA_ARGS_SIZE) {
		if(verbose) fprintf(stderr,
"Resultant command-line is too long for boot image, cannot continue\n");
		return 1;
	}

	memcpy(bootimg->cmdline, cmdline, BOOT_ARGS_SIZE);
	memcpy(bootimg->extra_cmdline, cmdline+BOOT_ARGS_SIZE, BOOT_EXTRA_ARGS_SIZE);


	if(fd) {
		if(lseek(fd, 0, SEEK_SET)) {
			if(verbose) fprintf(stderr, "Seek failed: %s\n",
strerror(errno));
			return 1;
		}

		if(write(fd, buf, blksz)!=blksz) {
			if(verbose) fprintf(stderr, "Write failed: %s\n",
strerror(errno));
			return 1;
		}
	} else {
		int32_t cnt=blksz;
		do {
			if(write(1, buf, blksz)!=cnt) {
				if(verbose) fprintf(stderr,
"Write failed: %s\n", strerror(errno));
				return 1;
			}

			if((cnt=read(0, buf, blksz))<0) {
				if(verbose) fprintf(stderr, "Read failed: %s\n",
strerror(errno));
				return 1;
			}
		} while(cnt>0);
		fd=1; /* check for errors on close() */
	}


	if(close(fd)) {
		if(verbose) fprintf(stderr, "Close failed: %s\n",
strerror(errno));
		return 1;
	}

	return 0;
}

