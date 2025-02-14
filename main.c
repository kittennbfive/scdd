#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <linux/usb/tmc.h>
#include <err.h>
#include <getopt.h>
#include <time.h>

/*
scdd (scope data dumper for Rigol MSO5000 series)

(c) 2025 by kittennbfive - https://github.com/kittennbfive

AGPLv3+ and NO WARRANTY!

Please read the fine manual.
*/

#define VERSION_STR "0.1"

#define SZ_BUF_DEVICENAME 20
#define DEFAULT_DEVICE "/dev/usbtmc0"
#define SZ_BUF_FILENAME 100
#define DEFAULT_CHANNEL 1

//static float x_timestep=0; //seconds, currently unused
static float y_offset=0;
static float y_factor=0;

#define BUFFER_SIZE 4096 //must be 4096, hardcoded in USBTMC Kernel driver... Other values make the transfer fail and/or the scope crash until power-cycle...

static void send_command(int fd, char const * const cmd)
{
	if(write(fd, cmd, strlen(cmd))<0)
		err(1, "send_command: write failed");
}

static void get_string(int fd, char const * const cmd, char * const out, const uint_fast32_t sz_out)
{
	if(write(fd, cmd, strlen(cmd))<0)
		err(1, "get_string: write failed");
	
	memset(out, '\0', sz_out);
	if(read(fd, out, sz_out-1)<=0)
		err(1, "get_string: read failed");
	
	if(strlen(out) && out[strlen(out)-1]=='\n')
		out[strlen(out)-1]='\0';
}

static float get_float(int fd, char const * const cmd)
{
	if(write(fd, cmd, strlen(cmd))<0)
		err(1, "get_float: write failed");
	
	char buf[32];
	memset(buf, '\0', 32);
	if(read(fd, buf, 31)<=0)
		err(1, "get_float: read failed");
	
	float val;
	sscanf(buf, "%f", &val);
	
	return val;
}

static bool get_bool(int fd, char const * const cmd)
{
	if(write(fd, cmd, strlen(cmd))<0)
		err(1, "get_bool: write failed");
	
	char buf;
	if(read(fd, &buf, 1)<=0)
		err(1, "get_bool: read failed");
	
	return (buf!='0');
}

static void convert_raw_write(FILE * out, const bool raw_float, uint8_t const * const buf, const uint_fast32_t sz)
{
	uint_fast32_t i;
	float val;
	for(i=0; i<sz; i++)
	{
		val=((float)buf[i]-(128+y_offset))*y_factor;
		if(!raw_float)
			fprintf(out, "%.2f\n", val);
		else
			fwrite(&val, sizeof(float), 1, out);
	}
}

static void print_usage_and_exit(void)
{
	printf("usage: scdd [--device $device] [--channel $channel] [--filename $filename] [raw-float]\n\n    device is %s by default\n    channel is %u by default\n    specify PIPE as filename to use stdout (for piping)\n    use raw-float to output unformated, raw float values (%lu bytes each)\n", DEFAULT_DEVICE, DEFAULT_CHANNEL, sizeof(float));
	exit(0);
}

int main(int argc, char ** argv)
{
	fprintf(stderr, "This is scdd version %s - data dumper for Rigol MSO5000 series\n(c) 2025 kittennbfive - github.com/kittennbfive/\nAGPLv3+ and NO WARRANTY\n\n", VERSION_STR);
	
	const struct option optiontable[]=
	{
		{ "device",		required_argument,	NULL,	0 },
		{ "channel",	required_argument,	NULL,	1 },
		{ "filename",	required_argument,	NULL,	2 },
		{ "raw-float",	no_argument,		NULL,	3 },
		
		{ "version",	no_argument,		NULL, 	100 },
		{ "help",		no_argument,		NULL, 	101 },
		{ "usage",		no_argument,		NULL, 	101 },
		
		{ NULL, 0, NULL, 0 }
	};
	
	char device[SZ_BUF_DEVICENAME+1]={'\0'};
	char filename[SZ_BUF_FILENAME+1]={'\0'};
	uint_fast8_t channel=1;
	bool write_to_stdout=false;
	bool output_raw_float=false;
	bool only_print_version=false;
	
	int optionindex;
	int opt;
	
	while((opt=getopt_long(argc, argv, "", optiontable, &optionindex))!=-1)
	{
		switch(opt)
		{
			case '?': print_usage_and_exit(); break;
			case 0: strncpy(device, optarg, SZ_BUF_DEVICENAME); device[SZ_BUF_DEVICENAME]='\0'; break;
			case 1: channel=atoi(optarg); break;
			case 2: strncpy(filename, optarg, SZ_BUF_FILENAME); filename[SZ_BUF_FILENAME]='\0'; break;
			case 3: output_raw_float=true; break;
			
			case 100: only_print_version=true; break;
			case 101: print_usage_and_exit(); break;
			
			default: errx(1, "don't know how to handle option %d - this is a bug", opt);
		}
	}
	
	if(only_print_version)
		return 0;
	
	if(channel<1 || channel>4)
		errx(1, "invalid channel %u", channel);
	
	if(strlen(device)==0) //use default device if none provided
	{
		fprintf(stderr, "no device specified, using default %s\n", DEFAULT_DEVICE);
		strncpy(device, DEFAULT_DEVICE, SZ_BUF_DEVICENAME);
		device[SZ_BUF_DEVICENAME]='\0';
	}
	
	if(strlen(filename)==0) //use sane default if none provided
	{
		//variable "device" contains a full path (and so '/' which are not allowed inside a *file*name), we only want the last part
		uint_fast16_t pos;
		for(pos=strlen(device); pos && device[pos]!='/'; pos--);
		if((pos+1)<strlen(device))
			pos++;
		time_t t=time(NULL);
		struct tm * tm=localtime(&t);
		snprintf(filename, SZ_BUF_FILENAME, "%s_ch%u_%02d.%02d_%02d%02d%02d.txt", &device[pos], channel, tm->tm_mday, tm->tm_mon+1, tm->tm_hour, tm->tm_min, tm->tm_sec); //don't use ':' inside filenames to maintain compatibility with FAT32
		filename[SZ_BUF_FILENAME]='\0';
	}
	else if(!strcmp(filename, "PIPE"))
	{
		write_to_stdout=true;
		fprintf(stderr, "output will be written to stdout\n");
	}
	
	if(output_raw_float)
		fprintf(stderr, "will output raw binary data (float, %lu bytes each)\n", sizeof(float));
	
	int fd=open(device, O_RDWR);
	if(fd<0)
		err(1, "opening device %s failed", device);
	
	fprintf(stderr, "reading data from channel %u\n", channel);
	
	char trig_state[10];
	get_string(fd, ":TRIG:STAT?", trig_state, 10);
	if(strcmp(trig_state, "STOP"))
	{
		fprintf(stderr, "scope is not in STOP mode, exiting...\n");
		close(fd);
		return 1;
	}
	
	char ch_select[20];
	sprintf(ch_select, ":CHAN%1u:DISP?", channel);
	if(!get_bool(fd, ch_select))
	{
		fprintf(stderr, "channel %u is not active, exiting...\n", channel);
		close(fd);
		return 1;
	}
	
	FILE * out=stdout;
	if(!write_to_stdout)
	{
		fprintf(stderr, "saving to file \"%s\"\n", filename);
		out=fopen(filename, "w");
		if(!out)
			err(1, "opening output file \"%s\" failed", filename);
	}
	
	//x_timestep=get_float(fd, ":WAV:XINC?"); //unused
	y_offset=get_float(fd, ":WAV:YOR?");
	y_factor=get_float(fd, ":WAV:YINC?");

	sprintf(ch_select, ":WAV:SOUR CHAN%1u", channel); //select channel
	send_command(fd, ch_select);
	send_command(fd, "WAV:MODE RAW"); //select RAW mode
	send_command(fd, "WAV:FORM BYTE"); //select output format
	send_command(fd, "WAV:STAR 1"); //select start position == beginning of capture
	
	uint8_t * buf=malloc(BUFFER_SIZE*sizeof(uint8_t));
	ssize_t nb_read;
	
	send_command(fd, "WAV:DATA?"); //request actual data
	
	nb_read=read(fd, buf, BUFFER_SIZE);
	if(nb_read<0 || nb_read<12) //USBTMC-header is 12 bytes
		err(1, "first read failed");
	
	if(buf[0]!='#')
		errx(1, "invalid header in response");
	uint_fast8_t nb_digits=buf[1]-'0';
	if(nb_digits==0)
		errx(1, "invalid number of digits in response");
	
	char size[10];
	memcpy(size, &buf[2], nb_digits*sizeof(char));
	size[2+nb_digits+1]='\0';
	
	uint_fast32_t sz_payload=atoi(size);
	if(sz_payload==0) //just in case atoi() gets accidentally fed with non-numerical characters...
		errx(1, "invalid payload size in response");
	
	fprintf(stderr, "sample memory is %lu bytes\n", sz_payload);
	
	convert_raw_write(out, output_raw_float, &buf[11], nb_read-11);
	
	ssize_t bytes_total;
	if(sz_payload<BUFFER_SIZE)
		bytes_total=0;
	else
		bytes_total=sz_payload-(BUFFER_SIZE-12); //BUFFER_SIZE bytes already read including 12 bytes USBTMC-header
	
	ssize_t nb_remaining=bytes_total;
	ssize_t nb_total=(BUFFER_SIZE-12);
	
	while(nb_remaining>0)
	{
		nb_read=read(fd, buf, MIN(nb_remaining, BUFFER_SIZE));
		if(nb_read<0)
			err(1, "read in loop failed");
		
		nb_remaining-=nb_read;
		nb_total+=nb_read;
		
		fprintf(stderr, "\r%lu bytes read...", nb_total);
		
		if(nb_remaining==0 && nb_read>0) //last read?
			convert_raw_write(out, output_raw_float, buf, nb_read-1); //ignore USBTMC-footer byte
		else
			convert_raw_write(out, output_raw_float, buf, nb_read);

	}
	
	fprintf(stderr, "\ndone, all fine\n");
	
	if(!write_to_stdout)
		fclose(out);
	
	close(fd);
	
	free(buf);
	
	return 0;
}
