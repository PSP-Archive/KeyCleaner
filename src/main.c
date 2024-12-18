#include <pspsdk.h>
#include <pspkernel.h>
#include <pspidstorage.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>


#define printf pspDebugScreenPrintf


#define VERS    1
#define REVS    4


#ifdef PSPFW150
/* 1.5 */
PSP_MODULE_INFO("KeyCleaner", 0x1000, VERS, REVS);
PSP_MAIN_THREAD_ATTR(0);
#else
/* 3.x */
PSP_MODULE_INFO("KeyCleaner", 0, VERS, REVS);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
#endif


#ifdef PSPFW150
/* 1.5 */
int ReadKey(int key, char *buffer)
{
        int err;

        memset(buffer, 0, 512);
        err = sceIdStorageReadLeaf(key, buffer);

        return err;
}

int WriteKey(int key, char *buffer)
{
        int err;

        err = sceIdStorageWriteLeaf(key, buffer);
        sceIdStorageFlush();

        return err;
}

int CreateKey(int key)
{
        int err;

        err = sceIdStorageCreateLeaf(key);
        sceIdStorageFlush();

        return err;
}

int DeleteKey(int key)
{
        int err;

        err = sceIdStorageDeleteLeaf(key);
        sceIdStorageFlush();

        return err;
}
#else
/* 3.x */
int ReadKey(int key, char *buffer);
int WriteKey(int key, char *buffer);
int CreateKey(int key);
int DeleteKey(int key);
#endif


/* Global Defines */

#define MOD_ADLER 65521

/* Global Variables */

u32 ic1003[480*272];
int ic1003Loaded;

/*********************************************************************/

unsigned int adler_32(unsigned char *data, int len)
{
    unsigned int a = 1, b = 0;
    int tlen;

    while (len) {
         tlen = len > 5550 ? 5550 : len;
         len -= tlen;
         do {
              a += *data++;
              b += a;
         } while (--tlen);
         a = (a & 0xffff) + (a >> 16) * (65536-MOD_ADLER);
         b = (b & 0xffff) + (b >> 16) * (65536-MOD_ADLER);
    }
    /* It can be shown that a <= 0x1013a here, so a single subtract will do. */
    if (a >= MOD_ADLER)
         a -= MOD_ADLER;
    /* It can be shown that b can reach 0xffef1 here. */
    b = (b & 0xffff) + (b >> 16) * (65536-MOD_ADLER);
    if (b >= MOD_ADLER)
         b -= MOD_ADLER;
    return (b << 16) | a;
}


int loadPngData(png_structp png_ptr)
{
    unsigned int sig_read = 0;
    png_uint_32 width, height, x, y;
    int bit_depth, color_type, interlace_type;
    png_infop info_ptr;
    u32* line;

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
        return -1;
    }
    png_set_sig_bytes(png_ptr, sig_read);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, int_p_NULL, int_p_NULL);
    if (width != 480 || height != 272) {
        png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
        return -1;
    }
    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
    line = (u32*) malloc(width * 4);
    if (!line) {
        png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
        return -1;
    }
    for (y = 0; y < height; y++) {
        png_read_row(png_ptr, (u8*) line, png_bytep_NULL);
        for (x = 0; x < width; x++)
            ic1003[x + y * 480] =  line[x];
    }
    free(line);
    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);

    return 0;
}


int loadPng(const char* filename)
{
    png_structp png_ptr;
    FILE *fp;
    int err;

    if ((fp = fopen(filename, "rb")) == NULL) return -1;
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fclose(fp);
        return -1;
    }
    png_init_io(png_ptr, fp);
    err = loadPngData(png_ptr);
    fclose(fp);

    return err;
}


void loadColors(void)
{
    int f;
    unsigned int bc, fc;

    f = sceIoOpen("textcolors.bin", PSP_O_RDONLY, 0777);
    if (f <= 0)
        return;
    sceIoRead(f, (char *)&bc, 4);
    sceIoRead(f, (char *)&fc, 4);
    sceIoClose(f);

    pspDebugScreenSetBackColor(bc);
    pspDebugScreenSetTextColor(fc);
}


void showPng(void)
{
    u32 *screen = (u32 *)0x44000000; /* uncached frame buffer */
    int x, y;

    for (y = 0; y < 272; y++)
        for (x = 0; x < 480; x++)
            screen[x + y * 512] = ic1003[x + y * 480];
}


void wait_release(unsigned int buttons)
{
    SceCtrlData pad;

    sceCtrlReadBufferPositive(&pad, 1);
    while (pad.Buttons & buttons)
    {
        sceKernelDelayThread(100000);
        sceCtrlReadBufferPositive(&pad, 1);
    }
}


unsigned int wait_press(unsigned int buttons)
{
    SceCtrlData pad;

    sceCtrlReadBufferPositive(&pad, 1);
    while (1)
    {
        if (pad.Buttons & buttons)
            return pad.Buttons & buttons;
        sceKernelDelayThread(100000);
        sceCtrlReadBufferPositive(&pad, 1);
    }
    return 0;   /* never reaches here, again, just to suppress warning */
}


int confirm_cancel(void)
{
    SceCtrlData pad;

    while (1)
    {
        sceKernelDelayThread(10000);
        sceCtrlReadBufferPositive(&pad, 1);
        if(pad.Buttons & PSP_CTRL_CROSS)
        {
            wait_release(PSP_CTRL_CROSS);
            return 1;
        }
        if(pad.Buttons & PSP_CTRL_CIRCLE)
        {
            wait_release(PSP_CTRL_CIRCLE);
            return 0;
        }
    }
    return 0;   /* never reaches here, this suppresses a warning */
}


void new_dir(char *dir_name)
{
    char new_name[512];
    int d, err, i;

    d = sceIoDopen(dir_name);

    if (d >= 0)
    {
        /* directory already exists, try to rename it */
        sceIoDclose(d);

        for (i=0; i<10000; i++)
        {
            sprintf(new_name, "%s_%04d", dir_name, i);
            d = sceIoDopen(new_name);
            if (d < 0)
                break; /* directory with this name doesn't exist */
            sceIoDclose(d);
        }
        if (i == 10000)
            printf("\n\n ERROR: Could not rename directory %s!\n\n", dir_name);
        else
        {
            printf("\n Renaming directory %s to %s...", dir_name, new_name);
            err = sceIoRename(dir_name, new_name);
            if (err < 0)
                printf("FAILED! Error code %X\n\n", err);
            else
                printf("PASSED!\n\n");
        }
    }

    sceIoMkdir(dir_name, 0777);
}


void dump_keys(void)
{
    char buffer[512];
    char filepath[32];
    int f, s, currkey;
    int linecnt = 5;

    pspDebugScreenClear();
    printf("\n       If keys directory already exists, it will be renamed.\n");
    printf("                O = back, X = dump keys to memstick\n\n");

    if (confirm_cancel())
    {
        new_dir("keys");
        for (currkey=0; currkey<0xfff0; currkey++)
        {
            s = ReadKey(currkey, buffer);
            if (s != 0) continue;
            sprintf(filepath, "keys/0x%04X.bin", currkey);
            f = sceIoOpen(filepath, PSP_O_WRONLY | PSP_O_CREAT, 0777);
            if (f <= 0) continue;
            printf(" Saving key %04X to file %s...", currkey, filepath);
            sceIoWrite(f, buffer, 512);
            sceIoClose(f);
            printf(" done.\n");

            linecnt++;
            if (linecnt >= 28)
            {
                sceKernelDelayThread(1*1000*1000);
                pspDebugScreenClear();
                linecnt = 0;
            }
        }
    }

    sceKernelDelayThread(5*1000*1000);
}


void new_key(int key, char *buffer, int model)
{
    if (key == 4 && (model == 79 || model == 82))
    {
        int i;
        int reconstruct[38] = {
            0,0x6E,1,0x79,2,0x72,3,0x42,4,0x01,8,0x10,12,0xBB,13,0x01,
            14,0xAB,15,0x1F,16,0xD8,18,0x24,20,0x14,21,0x31,22,0x14,
            24,0x94,25,0x01,26,0x48,28,0xD8
        };
        memset(buffer, 0, 512);
        for (i = 0; i < 38; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
    }
    else if (key == 4 && model == 85)
    {
        int i;
        int reconstruct[56] = {
            0,0x6E,1,0x79,2,0x72,3,0x42,4,0x01,8,0x3E,12,0x6F,13,0xE8,
            14,0xAA,15,0xB3,16,0xD8,18,0x24,20,0x14,21,0x31,22,0x14,
            24,0x94,26,0x48,28,0xD8,52,0x80,53,0x02,70,0x36,71,0x10,
            72,0xD2,73,0x0F,74,0x20,75,0x1C,76,0x3C,77,0x05
        };
        memset(buffer, 0, 512);
        for (i = 0; i < 56; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
    }
    else if (key == 5)
    {
        int i;
        int reconstruct[22] = {
            0,0x67,1,0x6B,2,0x6C,3,0x43,4,0x01,8,0x01,12,0xCA,13,0xD9,
            14,0xE3,15,0x9B,16,0x0A
        };
        memset(buffer, 0, 512);
        for (i = 0; i < 22; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
        if (model == 82)
            buffer[0] = 0x98; /* Chilly Willy patch */
    }
    else if (key == 6 && model == 79)
    {
        int i;
        int reconstruct[20] = {
            0,0x72,1,0x64,2,0x44,3,0x4D,4,0x01,8,0x03,12,0xFF,13,0xFF,
            14,0xFF,15,0xFF
        };
        memset(buffer, 0, 512);
        for (i = 0; i < 20; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
    }
    else if (key == 6 && (model == 82 || model == 85))
    {
        int i;
        int reconstruct[28] = {
            0,0x72,1,0x64,2,0x44,3,0x4D,4,0x01,8,0x07,12,0x85,13,0xBD,
            14,0x2c,15,0x75,19,0x85,20,0x83,21,0x81,22,0x80
        };
        memset(buffer, 0, 512);
        for (i = 0; i < 28; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
    }
    else if (key == 0x42)
    {
        memset(buffer, 0, 512);
    }
    else if (key == 0x43 && (model == 79 || model == 82))
    {
        int i;
        int reconstruct[36] = {
            0,0x55,1,0x73,2,0x74,3,0x72,4,0x53,5,0x6F,6,0x6E,7,0x79,
            12,0x50,13,0x53,14,0x50,28,0x31,29,0x2E,30,0x30,31,0x30,
            32,0x50,34,0x53,36,0x50
        };
        memset(buffer, 0, 512);
        memset(buffer, 0x20, 28);
        for (i = 0; i < 36; i+=2)
            buffer[reconstruct[i]]=reconstruct[i+1];
    }
    else if (key == 0x45 && (model == 79 || model == 82))
    {
    	unsigned int b;

        memset(buffer, 0, 512);
        buffer[2] = 0x01;
	    printf("\n What model is your PSP? O = PSP-1000, X = PSP-1001, %c = PSP-1004/6\n", 0xc8);
    	b = wait_press(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE);
    	wait_release(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE);
	    switch (b)
    	{
    	    case PSP_CTRL_CROSS:
    	        buffer[0] = 0;
    	        break;
    	    case PSP_CTRL_SQUARE:
    	        buffer[0] = 2;
    	        break;
    	    case PSP_CTRL_CIRCLE:
    	        buffer[0] = 3;
    	        break;
    	    default:
    	        buffer[0] = 0;
    	        break;
    	}
    	printf(" wlan region key generated...");
    }
    else if (key == 0x46)
    {
        memset(buffer, 0, 512);
    }
    else if (key == 0x47)
    {
        memset(buffer, 0, 512);
        buffer[0] = 9;
    }
    else
    {
        memset(buffer, 0, 512);
		printf("no key generated...\n");
    }
}


int get_key_id(char *buffer)
{
    switch (adler_32((unsigned char*)buffer, 512))
    {
        case 0x1FD3063D:
        return 4;
        break;
        case 0x7E5309BE:
        return 0x00010004; /* key 4 for 85 */
        break;
        case 0x31D304AF:
        return 5; /* unpatched key 5 */
        break;
        case 0x93D304E0:
        return 0x00010005; /* Chilly Willy patched key 5 */
        break;
        case 0x2BD604AC:
        return 0x00020005; /* harleyg patched key 5 */
        break;
        case 0x98DD0568:
        return 6;
        break;
        case 0x73BF055C:
        return 0x00010006; /* key 6 for 82/86/85 */
        break;
        case 0xC54015F9:
        return 0x41;
        break;
        case 0x41CB176C:
        return 0x00010041; /* key 0x41 for 85 */
        break;
        case 0x02000001:
        return 0; /* special case - clear */
        break;
        case 0xC557081D:
        return 0x43;
        break;
        case 0x5DB9110A:
        return 0x00010043; /* key 0x43 for 85 */
        break;
        case 0x03FE0002:
        return 0x00010045; /* WLAN for PSP-1001/2 */
        break;
        case 0x05FE0003:
        return 0x00020045; /* WLAN for PSP-100? */
        break;
        case 0x07FE0004:
        return 0x00040045; /* WLAN for PSP-1004/6 */
        break;
        case 0x09FE0005:
        return 0x00000045; /* WLAN for PSP-1000 */
        break;
        case 0x1400000A:
        return 0x47;
        break;
        default:
        return -1;
    }

}


int is_orig_hd(void)
{
    char buffer[512];

    ReadKey(0x0004, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0005, buffer);
    if (get_key_id(buffer) != 4) /* look for copy of key 4 */
        return 0;
    ReadKey(0x0006, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 5) /* look for copy of any key 5 */
        return 0;
    ReadKey(0x0041, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 6) /* look for copy of any key 6 */
        return 0;
    ReadKey(0x0042, buffer);
    if (get_key_id(buffer) != 0x41) /* look for copy of key 0x41 */
        return 0;
    ReadKey(0x0043, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0045, buffer);
    if (get_key_id(buffer) != 0x47) /* look for copy of key 0x47 */
        return 0;
    ReadKey(0x0046, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 0x45) /* look for copy of any key 0x45 */
        return 0;
    if (ReadKey(0x0047, buffer) == 0) /* look for no key */
        return 0;
    return 1;
}


void fix_orig_hd(void)
{
    char buffer[512];

    printf("\n Are you really sure you wish to do this?\n");
    printf(" Writing the PSP keys has the potential to brick it.\n");
    printf(" Last chance to back out... O = skip, X = go for it!\n\n");
    if (!confirm_cancel())
        return;

    printf(" Fixing key 0x0047...");
    ReadKey(0x0045, buffer);
    WriteKey(0x0047, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0045...");
    ReadKey(0x0046, buffer);
    WriteKey(0x0045, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0046...");
    ReadKey(0x0004, buffer);
    WriteKey(0x0046, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0004...");
    ReadKey(0x0005, buffer);
    WriteKey(0x0004, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0005...");
    ReadKey(0x0006, buffer);
    buffer[1] = (char)0x6b;
    buffer[0] = (char)0x98;     /* make sure key 0x0005 is Chilly Willy'd */
    WriteKey(0x0005, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0006...");
    new_key(6, buffer, 82);
    WriteKey(0x0006, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0041...");
    ReadKey(0x0042, buffer);
    WriteKey(0x0041, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0042...");
    ReadKey(0x0043, buffer);
    WriteKey(0x0042, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0043...");
    new_key(0x0043, buffer, 82);
    WriteKey(0x0043, buffer);
    printf("fixed!\n");

    printf(" All keys fixed!\n");
    sceKernelDelayThread(5*1000*1000);
}


int is_orig_sd(void)
{
    char buffer[512];

    ReadKey(0x0004, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0005, buffer);
    if (get_key_id(buffer) != 4) /* look for copy of key 4 */
        return 0;
    ReadKey(0x0006, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 5) /* look for copy of any key 5 */
        return 0;
    ReadKey(0x0041, buffer);
    if (get_key_id(buffer) != 0x00010006) /* look for copy of 82/86 key 6 */
        return 0;
    ReadKey(0x0042, buffer);
    if (get_key_id(buffer) != 0x41) /* look for copy of key 0x41 */
        return 0;
    ReadKey(0x0043, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0045, buffer);
    if (get_key_id(buffer) != 0x47) /* look for copy of key 0x47 */
        return 0;
    ReadKey(0x0046, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 0x45) /* look for copy of any key 0x45 */
        return 0;
    ReadKey(0x0047, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    return 1;
}


void fix_orig_sd(void)
{
    char buffer[512];

    printf("\n Are you really sure you wish to do this?\n");
    printf(" Writing the PSP keys has the potential to brick it.\n");
    printf(" Last chance to back out... O = skip, X = go for it!\n\n");
    if (!confirm_cancel())
        return;

    printf(" Fixing key 0x0047...");
    ReadKey(0x0045, buffer);
    WriteKey(0x0047, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0045...");
    ReadKey(0x0046, buffer);
    WriteKey(0x0045, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0046...");
    ReadKey(0x0004, buffer);
    WriteKey(0x0046, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0004...");
    ReadKey(0x0005, buffer);
    WriteKey(0x0004, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0005...");
    ReadKey(0x0006, buffer);
    buffer[1] = (char)0x6b;
    buffer[0] = (char)0x98;     /* make sure key 0x0005 is Chilly Willy'd */
    WriteKey(0x0005, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0006...");
    ReadKey(0x0041, buffer);
    WriteKey(0x0006, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0041...");
    ReadKey(0x0042, buffer);
    WriteKey(0x0041, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0042...");
    ReadKey(0x0043, buffer);
    WriteKey(0x0042, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0043...");
    new_key(0x0043, buffer, 82);
    WriteKey(0x0043, buffer);
    printf("fixed!\n");

    printf(" All keys fixed!\n");
    sceKernelDelayThread(5*1000*1000);
}


int is_noobz_sd(void)
{
    char buffer[512];

    ReadKey(0x0004, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0005, buffer);
    if (get_key_id(buffer) != 4) /* look for copy of key 4 */
        return 0;
    ReadKey(0x0006, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 5) /* look for copy of any key 5 */
        return 0;
    ReadKey(0x0041, buffer);
    if (get_key_id(buffer) != 0x41) /* look for key 0x41 */
        return 0;
    ReadKey(0x0042, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0043, buffer);
    if (get_key_id(buffer) != 0x43) /* look for key 0x43 */
        return 0;
    ReadKey(0x0045, buffer);
    if ((get_key_id(buffer) & 0xFFFF) != 0x45) /* look for any key 0x45 */
        return 0;
    ReadKey(0x0046, buffer);
    if (get_key_id(buffer) != 0) /* look for clear key */
        return 0;
    ReadKey(0x0047, buffer);
    if (get_key_id(buffer) != 0x47) /* look for key 0x47 */
        return 0;
    return 1;
}


void fix_noobz_sd(void)
{
    char buffer[512];

    printf("\n Are you really sure you wish to do this?\n");
    printf(" Writing the PSP keys has the potential to brick it.\n");
    printf(" Last chance to back out... O = skip, X = go for it!\n\n");
    if (!confirm_cancel())
        return;

    printf(" Fixing key 0x0004...");
    ReadKey(0x0005, buffer);
    WriteKey(0x0004, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0005...");
    ReadKey(0x0006, buffer);
    buffer[1] = (char)0x6b;
    buffer[0] = (char)0x98;     /* make sure key 0x0005 is Chilly Willy'd */
    WriteKey(0x0005, buffer);
    printf("fixed!\n");

    printf(" Fixing key 0x0006...");
    new_key(0x0006, buffer, 82);
    WriteKey(0x0006, buffer);
    printf("fixed!\n");

    printf(" All keys fixed!\n");
    sceKernelDelayThread(5*1000*1000);
}


void analyze_7981()
{
    int err;
    char buffer[512];
    int failed = 0;
    unsigned int b;

    printf(" Checking key 0x0004...");
    err = ReadKey(4, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 4)
        printf(" okay!\n");
    else
    {
        failed = 1;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0005...");
    err = ReadKey(5, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 5)
        printf(" okay!\n");
    else
    {
        failed |= 2;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0006...");
    err = ReadKey(6, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 6)
        printf(" okay!\n");
    else
    {
        failed |= 4;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0041...");
    err = ReadKey(0x41, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x41)
        printf(" okay!\n");
    else
    {
        failed |= 8;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0042...");
    err = ReadKey(0x42, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 16;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0043...");
    err = ReadKey(0x43, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x43)
        printf(" okay!\n");
    else
    {
        failed |= 32;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0045...");
    err = ReadKey(0x45, buffer);
    if (!err)
        err = get_key_id(buffer);
    if ((err & 0xFFFF) == 0x45)
        printf(" okay!\n");
    else
    {
        failed |= 64;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0046...");
    err = ReadKey(0x46, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 128;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0047...");
    err = ReadKey(0x47, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x47)
        printf(" okay!\n");
    else
    {
        failed |= 256;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    if (failed)
    {
        if (failed == 0x100)
            printf("\n\n Don't worry about key 0x47 failing. This is just an old TA-079.\n");
        else if (failed == 0x180)
            printf("\n\n Wow, this is a REALLY old TA-079. The keys are fine.\n");
        sceKernelDelayThread(5*1000*1000);
        pspDebugScreenClear();
        if (failed & 0x001)
        {
            printf("\n Your PSP appears to have a bad key 0x0004.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0004...");
                new_key(4, buffer, 79);
                WriteKey(4, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x002)
        {
            printf("\n Your PSP appears to have a bad key 0x0005.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0005...");
                new_key(5, buffer, 79);
                WriteKey(5, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x004)
        {
            printf("\n Your PSP appears to have a bad key 0x0006.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0006...");
                new_key(6, buffer, 79);
                WriteKey(6, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x010)
        {
            printf("\n Your PSP appears to have a bad key 0x0042.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0042...");
                new_key(0x42, buffer, 79);
                WriteKey(0x42, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x020)
        {
            printf("\n Your PSP appears to have a bad key 0x0043.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0043...");
                new_key(0x43, buffer, 79);
                WriteKey(0x43, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x080)
        {
            printf("\n Your PSP appears to have a bad key 0x0046.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0046...");
                new_key(0x46, buffer, 79);
                WriteKey(0x46, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x100)
        {
            printf("\n Your PSP appears to have a bad key 0x0047.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0047...");
                new_key(0x47, buffer, 79);
                WriteKey(0x47, buffer);
                printf(" done!\n");
            }
        }

        printf("\n Press any key to return to main menu.\n");
        b = wait_press(0xFFFF);
        wait_release(0xFFFF);
        return;
    }
    else
        printf("\n\n Congratulations! Your keys appear to be fine. \n");

    sceKernelDelayThread(5*1000*1000);
}


void analyze_8286()
{
    int err;
    char buffer[512];
    int failed = 0;
    unsigned int b;

    printf(" Checking key 0x0004...");
    err = ReadKey(4, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 4)
        printf(" okay!\n");
    else
    {
        failed = 1;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0005...");
    err = ReadKey(5, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010005)
        printf(" okay! Chilly Willy patched key 5 found.\n");
    else
    {
        failed |= 2;
        if (err == 5)
            printf(" okay! Original unpatched key 5 found.\n");
        else if (err == 0x00020005)
            printf(" okay! Generic patched key 5 found.\n");
        else
        {
            printf(" failed!");
            if (err < 0)
                printf(" ReadKey returned code 0x%08X.\n", err);
            if (err == 0)
                printf(" This key is clear and shouldn't be.\n");
            if (err > 0)
                printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
        }
    }

    printf(" Checking key 0x0006...");
    err = ReadKey(6, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010006)
        printf(" okay!\n");
    else
    {
        failed |= 4;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0041...");
    err = ReadKey(0x41, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x41)
        printf(" okay!\n");
    else
    {
        failed |= 8;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0042...");
    err = ReadKey(0x42, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 16;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0043...");
    err = ReadKey(0x43, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x43)
        printf(" okay!\n");
    else
    {
        failed |= 32;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0045...");
    err = ReadKey(0x45, buffer);
    if (!err)
        err = get_key_id(buffer);
    if ((err & 0xFFFF) == 0x45)
        printf(" okay!\n");
    else
    {
        failed |= 64;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0046...");
    err = ReadKey(0x46, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 128;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0047...");
    err = ReadKey(0x47, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x47)
        printf(" okay!\n");
    else
    {
        failed |= 256;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    if (failed)
    {
        if (is_orig_hd())
        {
            printf("\n The PSP appears to be hard-downed. Press any key to fix.\n");
            b = wait_press(0xFFFF);
            wait_release(0xFFFF);
            pspDebugScreenClear();
            fix_orig_hd();
            return;
        }
        else if (is_orig_sd())
        {
            printf("\n The PSP appears to be original soft-downed. Press any key to fix.\n");
            b = wait_press(0xFFFF);
            wait_release(0xFFFF);
            pspDebugScreenClear();
            fix_orig_sd();
            return;
        }
        else if (is_noobz_sd())
        {
            printf("\n The PSP appears to be noobz soft-downed. Press any key to fix.\n");
            b = wait_press(0xFFFF);
            wait_release(0xFFFF);
            pspDebugScreenClear();
            fix_noobz_sd();
            return;
        }
        sceKernelDelayThread(5*1000*1000);
        pspDebugScreenClear();
        if (failed & 0x001)
        {
            printf("\n Your PSP appears to have a bad key 0x0004.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0004...");
                new_key(4, buffer, 82);
                WriteKey(4, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x002)
        {
            printf("\n Your PSP appears to not have a Chilly Willy patched key 0x0005.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0005...");
                new_key(5, buffer, 82);
                WriteKey(5, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x004)
        {
            printf("\n Your PSP appears to have a bad key 0x0006.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0006...");
                new_key(6, buffer, 82);
                WriteKey(6, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x010)
        {
            printf("\n Your PSP appears to have a bad key 0x0042.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0042...");
                new_key(0x42, buffer, 82);
                WriteKey(0x42, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x020)
        {
            printf("\n Your PSP appears to have a bad key 0x0043.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0043...");
                new_key(0x43, buffer, 82);
                WriteKey(0x43, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x040)
        {
            printf("\n Your PSP appears to have a bad key 0x0045.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0045...");
                new_key(0x45, buffer, 82);
                WriteKey(0x45, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x080)
        {
            printf("\n Your PSP appears to have a bad key 0x0046.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0046...");
                new_key(0x46, buffer, 82);
                WriteKey(0x46, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x100)
        {
            printf("\n Your PSP appears to have a bad key 0x0047.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0047...");
                new_key(0x47, buffer, 82);
                WriteKey(0x47, buffer);
                printf(" done!\n");
            }
        }

        printf("\n Press any key to return to main menu.\n");
        b = wait_press(0xFFFF);
        wait_release(0xFFFF);
        return;
    }
    else
    {
        printf("\n Congratulations! Your keys appear to be fine. \n");

        err = ReadKey(5, buffer);
        if (!err)
            err = get_key_id(buffer);
        if (err == 0x00010005)
        {
            printf("\n Do you wish to unpatch key 0x0005? Please note that an unpatched\n");
            printf("  key 0x0005 will brick a TA-082/86 with 1.50 or custom firmware\n");
            printf("  unless you have installed a custom IPL to prevent this.\n\n");
            printf("                  O = Leave as is, X = unpatch key\n\n");
            if (confirm_cancel())
            {
                printf("\n Unpatching key 0x0005...");
                new_key(5, buffer, 79); /* 79 is unpatched */
                buffer[0] = 0x67;
                WriteKey(5, buffer);
                printf(" done!\n");
            }
        }
    }

    sceKernelDelayThread(5*1000*1000);
}


void analyze_85()
{
    int err;
    char buffer[512];
    int failed = 0;
    unsigned int b;

    printf(" Checking key 0x0004...");
    err = ReadKey(4, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010004)
        printf(" okay!\n");
    else
    {
        failed = 1;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0005...");
    err = ReadKey(5, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 5)
        printf(" okay!\n");
    else
    {
        failed |= 2;
        if (err == 5)
            printf(" okay! Original unpatched key 5 found.\n");
        else if (err == 0x00020005)
            printf(" okay! Generic patched key 5 found.\n");
        else
        {
            printf(" failed!");
            if (err < 0)
                printf(" ReadKey returned code 0x%08X.\n", err);
            if (err == 0)
                printf(" This key is clear and shouldn't be.\n");
            if (err > 0)
                printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
        }
    }

    printf(" Checking key 0x0006...");
    err = ReadKey(6, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010006)
        printf(" okay!\n");
    else
    {
        failed |= 4;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0041...");
    err = ReadKey(0x41, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010041)
        printf(" okay!\n");
    else
    {
        failed |= 8;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0042...");
    err = ReadKey(0x42, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 16;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0043...");
    err = ReadKey(0x43, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x00010043)
        printf(" okay!\n");
    else
    {
        failed |= 32;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

#if 0
    printf(" Checking key 0x0045...");
    err = ReadKey(0x45, buffer);
    if (!err)
        err = get_key_id(buffer);
    if ((err & 0xFFFF) == 0x45)
        printf(" okay!\n");
    else
    {
        failed |= 64;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }
#endif

    printf(" Checking key 0x0046...");
    err = ReadKey(0x46, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0)
        printf(" okay!\n");
    else
    {
        failed |= 128;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        else
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    printf(" Checking key 0x0047...");
    err = ReadKey(0x47, buffer);
    if (!err)
        err = get_key_id(buffer);
    if (err == 0x47)
        printf(" okay!\n");
    else
    {
        failed |= 256;
        printf(" failed!");
        if (err < 0)
            printf(" ReadKey returned code 0x%08X.\n", err);
        if (err == 0)
            printf(" This key is clear and shouldn't be.\n");
        if (err > 0)
            printf(" This key is a copy of key 0x%04X.\n", err & 0xFFFF);
    }

    if (failed)
    {
        sceKernelDelayThread(5*1000*1000);
        pspDebugScreenClear();
        if (failed & 0x001)
        {
            printf("\n Your PSP appears to have a bad key 0x0004.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0004...");
                new_key(4, buffer, 85);
                WriteKey(4, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x002)
        {
            printf("\n Your PSP appears to have a bad key 0x0005.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0005...");
                new_key(5, buffer, 85);
                WriteKey(5, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x004)
        {
            printf("\n Your PSP appears to have a bad key 0x0006.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0006...");
                new_key(6, buffer, 85);
                WriteKey(6, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x010)
        {
            printf("\n Your PSP appears to have a bad key 0x0042.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0042...");
                new_key(0x42, buffer, 85);
                WriteKey(0x42, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x080)
        {
            printf("\n Your PSP appears to have a bad key 0x0046.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0046...");
                new_key(0x46, buffer, 85);
                WriteKey(0x46, buffer);
                printf(" done!\n");
            }
        }
        if (failed & 0x100)
        {
            printf("\n Your PSP appears to have a bad key 0x0047.\n");
            printf("\n                    O = Leave as is, X = Fix key\n\n");
            if (confirm_cancel())
            {
                printf("\n Fixing key 0x0047...");
                new_key(0x47, buffer, 85);
                WriteKey(0x47, buffer);
                printf(" done!\n");
            }
        }

        printf("\n Press any key to return to main menu.\n");
        b = wait_press(0xFFFF);
        wait_release(0xFFFF);
        return;
    }
    else
    {
        printf("\n Congratulations! Your keys appear to be fine. \n");
    }

    sceKernelDelayThread(5*1000*1000);
}


/*
 * return values:
 * 0x0101 = TA_079/81
 * 0x0202 = TA_082/86
 * 0x0303 = TA_085
 *
 */

int check_mobo(void)
{
    int mobo;
    char buffer[512];

    ReadKey(0x0100, buffer);
    mobo = buffer[0x03f];

    ReadKey(0x0050, buffer);
    mobo = (mobo << 8) | buffer[0x021];

    return mobo;
}


/*
 * return values:
 * 0x03 = PSP-1000 Japan
 * 0x04 = PSP-1001 USA
 * 0x05 = PSP-1003/4 UK, Europe, Middle East, Africa
 * 0x06 = PSP-1005 Korea
 * 0x07 = PSP-1003 UK
 * 0x09 = PSP-1002 Australia, New Zealand
 * 0x0A = PSP-1006 Hong Kong, Singapore
 *
 */

int check_region(void)
{
    char buffer[512];

    ReadKey(0x0120, buffer);
    return buffer[0x03d];
}


void analyze_keys(void)
{
    unsigned int b;

    while (1)
    {
        pspDebugScreenClear();
        printf("\n");
        switch (check_mobo())
        {
            case 0x0101:
            printf(" The IdStorage identifies the motherboard as a TA-079/81\n\n");
            break;
            case 0x0202:
            printf(" The IdStorage identifies the motherboard as a TA-082/86\n\n");
            break;
            case 0x0303:
            printf(" The IdStorage identifies the motherboard as a TA-085\n\n");
            break;
            default:
            printf(" The motherboard cannot be determined from the IdStorage\n");
            printf(" There may be a problem with the IdStorage on this PSP.\n\n");
        }
        printf(" Select the motherboard in your PSP. If you aren't sure, choose \n");
        printf(" 'Show Picture' and note the red rectangle. If you turn your PSP\n");
        printf(" around, open the UMD door, and examine the corresponding area on\n");
        printf(" your PSP. You will see no writing if you have a TA-079 or 81, or\n");
        printf(" you will see 'IC1003' printed upside down if you a TA-082 or 86.\n");
        printf(" Please note that all slim PSPs are currently TA-085 motherboards.\n");
        printf("\n     O = TA-079/81, X = TA-082/86, %c = TA-085, %c = Show Picture\n\n", 0xd8, 0xc8);
        b = wait_press(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE|PSP_CTRL_TRIANGLE);
        wait_release(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE|PSP_CTRL_TRIANGLE);
        if (b & PSP_CTRL_CIRCLE)
        {
            analyze_7981();
            return;
        }
        if (b & PSP_CTRL_CROSS)
        {
            analyze_8286();
            return;
        }
        if (b & PSP_CTRL_TRIANGLE)
        {
            analyze_85();
            return;
        }
        if (b & PSP_CTRL_SQUARE)
            if (ic1003Loaded)
            {
                pspDebugScreenClear();
                showPng();
                b = wait_press(0xffff);
                wait_release(0xffff);
            }
    }
}


int main(void)
{
    unsigned int b;

    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0x00ffffff);
    pspDebugScreenClear();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    if (scePowerGetBatteryLifePercent() < 75)
    {
        printf("\n Battery is %d%%, it should be at least at 75%%.\n", scePowerGetBatteryLifePercent());
        printf(" WARNING! Using this program with a low battery can brick the PSP!\n");
        printf(" Press X to continue anyway, press anything else to quit.\n");
        b = wait_press(0xffff);
        wait_release(0xffff);
        if (!(b & PSP_CTRL_CROSS))
        {
            sceKernelDelayThread(1*1000*1000);
            sceKernelExitGame();
        }
    }

#ifndef PSPFW150
    SceUID mod = pspSdkLoadStartModule("idstorage.prx", PSP_MEMORY_PARTITION_KERNEL);
    if (mod < 0)
    {
        printf(" Error 0x%08X loading/starting idstorage.prx.\n", mod);
        sceKernelDelayThread(3*1000*1000);
        sceKernelExitGame();
    }
#endif

    loadColors();
    ic1003Loaded = !loadPng("IC1003.PNG");

    while (1)
    {
        pspDebugScreenClear();
        printf("\n                         Key Cleaner v%d.%d\n", VERS, REVS);
        printf("                         by  Chilly Willy\n\n");

        switch (check_mobo())
        {
            case 0x0101:
            printf(" The IdStorage identifies the motherboard as a TA-079/81\n");
            break;
            case 0x0202:
            printf(" The IdStorage identifies the motherboard as a TA-082/86\n");
            break;
            case 0x0303:
            printf(" The IdStorage identifies the motherboard as a TA-085\n");
            break;
            default:
            printf(" The motherboard cannot be determined from the IdStorage\n");
            printf(" There may be a problem with the IdStorage on this PSP.\n");
        }

        switch (check_region())
        {
            case 0x3:
            printf(" The IdStorage identifies the region set to a Japanese PSP-%c000\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0x4:
            printf(" The IdStorage identifies the region set to an American PSP-%c001\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0x5:
            printf(" The IdStorage identifies the region set to a European PSP-%c003/4\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0x6:
            printf(" The IdStorage identifies the region set to a Korean PSP-%c005\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0x7:
            printf(" The IdStorage identifies the region set to a UK PSP-%c003\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0x9:
            printf(" The IdStorage identifies the region set to a Australian PSP-%c002\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            case 0xA:
            printf(" The IdStorage identifies the region set to a Hong Kong PSP-%c006\n", check_mobo() == 0x0303 ? '2' : '1');
            break;
            default:
            printf(" The IdStorage identifies the region set to an unknown PSP-%c00?\n", check_mobo() == 0x0303 ? '2' : '1');
            printf(" Please report the code 0x%02X and your PSP region and model.\n", check_region());
        }

        printf("\n\n             O = Exit, X = Analyze keys, %c = Dump keys\n\n", 0xc8);
        b = wait_press(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE);
        wait_release(PSP_CTRL_SQUARE|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE);
        if (b & PSP_CTRL_CROSS)
            analyze_keys();
        if (b & PSP_CTRL_SQUARE)
            dump_keys();
        if (b & PSP_CTRL_CIRCLE)
        {
            printf("\n\n Exiting application. Please wait for the XMB to reload.\n");
            sceKernelDelayThread(2*1000*1000);
            sceKernelExitGame();
        }
    }

    return 0;   /* never reaches here, again, just to suppress warning */
}
