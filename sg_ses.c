/*
 * Copyright (c) 2004 Douglas Gilbert.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "sg_include.h"
#include "sg_lib.h"
#include "sg_cmds.h"

/* A utility program for the Linux OS SCSI subsystem.
 *
 * This program issues SCSI SEND DIAGNOSTIC and RECEIVE DIAGNOSTIC commands
 * tailored for SES (enclosure) devices.
 */

static char * version_str = "1.08 20041026";

#define SEND_DIAGNOSTIC_CMD     0x1d
#define SEND_DIAGNOSTIC_CMDLEN  6
#define RECEIVE_DIAGNOSTIC_CMD     0x1c
#define RECEIVE_DIAGNOSTIC_CMDLEN  6

#define SENSE_BUFF_LEN 32       /* Arbitrary, could be larger */
#define DEF_TIMEOUT 60000       /* 60,000 millisecs == 60 seconds */
#define MX_ALLOC_LEN 4096
#define MX_ELEM_HDR 512

#define ME "sg_ses: "

#define EBUFF_SZ 256


static struct option long_options[] = {
        {"byte1", 1, 0, 'b'},
        {"control", 0, 0, 'c'},
        {"data", 1, 0, 'd'},
        {"filter", 0, 0, 'f'},
        {"help", 0, 0, 'h'},
        {"hex", 0, 0, 'H'},
        {"inner-hex", 0, 0, 'i'},
        {"list", 0, 0, 'l'},
        {"page", 1, 0, 'p'},
        {"raw", 0, 0, 'r'},
        {"status", 0, 0, 's'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {0, 0, 0, 0},
};

static void usage()
{
    fprintf(stderr, "Usage: "
          "sg_ses [--byte1=<n>] [--control] [--data=<h>...] [--filter] "
          "[--help]\n"
          "              [--hex] [--inner-hex] [--list] [--page=<n>] [--raw] "
          "[--status]\n"
          "              [--verbose] [--version] <scsi_device>\n"
          "  where: --byte1=<n>|-b <n> byte 1 (2nd byte) for some control "
          "pages\n"
          "         --control|-c       send control information\n"
          "         --help|-h          print out usage message\n"
          "         --data=<h>...|-d <h>...    string of hex for control "
          "pages\n"
          "         --filter|-f        filter out enclosure status clear "
          "flags\n"
          "         --hex|-H           print status response in hex\n"
          "         --inner-hex|-i     print innermost level of a"
          " status page in hex\n"
          "         --list|-l          list known pages and elements (ignore"
          " device)\n"
          "         --page=<n>|-p <n>  page code value <n> (def: 0)\n"
          "         --raw|-r           print status page in hex suitable "
          "for '-d'\n"
          "         --status|-s        fetch status information\n"
          "         --verbose|-v       increase verbosity\n"
          "         --version|-V       print version string and exit\n"
          );
}

/* Returns 0 for success, else -1 */
static int do_senddiag(int sg_fd, int pf_bit, void * outgoing_pg, 
                       int outgoing_len, int noisy, int verbose)
{
    int k, res;
    unsigned char senddiagCmdBlk[SEND_DIAGNOSTIC_CMDLEN] = 
        {SEND_DIAGNOSTIC_CMD, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_io_hdr io_hdr;

    /* not interested in self test bit/code or associates */
    senddiagCmdBlk[1] = (unsigned char)(pf_bit << 4);
    senddiagCmdBlk[3] = (unsigned char)((outgoing_len >> 8) & 0xff);
    senddiagCmdBlk[4] = (unsigned char)(outgoing_len & 0xff);
   if (verbose) {
        fprintf(stderr, "    Send diagnostic cdb: ");
        for (k = 0; k < SEND_DIAGNOSTIC_CMDLEN; ++k)
            fprintf(stderr, "%02x ", senddiagCmdBlk[k]);
        fprintf(stderr, "\n");
        if (verbose > 1) {
            fprintf(stderr, "    Send diagnostic parameter block:\n");
            dStrHex(outgoing_pg, outgoing_len, 0);
        }
    }

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = SEND_DIAGNOSTIC_CMDLEN;
    io_hdr.mx_sb_len = sizeof(sense_b);
    io_hdr.dxfer_direction = outgoing_len ? SG_DXFER_TO_DEV : SG_DXFER_NONE;
    io_hdr.dxfer_len = outgoing_len;
    io_hdr.dxferp = outgoing_pg;
    io_hdr.cmdp = senddiagCmdBlk;
    io_hdr.sbp = sense_b;
    io_hdr.timeout = DEF_TIMEOUT;

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("SG_IO (send diagnostic) error");
        return -1;
    }
    res = sg_err_category3(&io_hdr);
    switch (res) {
    case SG_LIB_CAT_CLEAN:
    case SG_LIB_CAT_RECOVERED:
        return 0;
    default:
        if (noisy) {
            char ebuff[EBUFF_SZ];
            snprintf(ebuff, EBUFF_SZ, "Send diagnostic error, pf_bit=%d",
                     pf_bit);
            sg_chk_n_print3(ebuff, &io_hdr);
        }
        return -1;
    }
}

static int do_rcvdiag(int sg_fd, int pcv, int pg_code, void * resp, 
                      int mx_resp_len, int noisy, int verbose)
{
    int k, res;
    unsigned char rcvdiagCmdBlk[RECEIVE_DIAGNOSTIC_CMDLEN] = 
        {RECEIVE_DIAGNOSTIC_CMD, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_io_hdr io_hdr;

    rcvdiagCmdBlk[1] = (unsigned char)(pcv ? 0x1 : 0);
    rcvdiagCmdBlk[2] = (unsigned char)(pg_code);
    rcvdiagCmdBlk[3] = (unsigned char)((mx_resp_len >> 8) & 0xff);
    rcvdiagCmdBlk[4] = (unsigned char)(mx_resp_len & 0xff);
   if (verbose) {
        fprintf(stderr, "    Receive diagnostic cdb: ");
        for (k = 0; k < RECEIVE_DIAGNOSTIC_CMDLEN; ++k)
            fprintf(stderr, "%02x ", rcvdiagCmdBlk[k]);
        fprintf(stderr, "\n");
    }

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = RECEIVE_DIAGNOSTIC_CMDLEN;
    io_hdr.mx_sb_len = sizeof(sense_b);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = mx_resp_len;
    io_hdr.dxferp = resp;
    io_hdr.cmdp = rcvdiagCmdBlk;
    io_hdr.sbp = sense_b;
    io_hdr.timeout = DEF_TIMEOUT;

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("SG_IO (receive diagnostic) error");
        return -1;
    }
    res = sg_err_category3(&io_hdr);
    switch (res) {
    case SG_LIB_CAT_CLEAN:
    case SG_LIB_CAT_RECOVERED:
        return 0;
    default:
        if (noisy) {
            char ebuff[EBUFF_SZ];
            snprintf(ebuff, EBUFF_SZ, "Receive diagnostic error, pcv=%d, "
                     "page_code=%x ", pcv, pg_code);
            sg_chk_n_print3(ebuff, &io_hdr);
        }
        return -1;
    }
}

static const char * scsi_ptype_strs[] = {
    /* 0 */ "disk",
    "tape",
    "printer",
    "processor",
    "write once optical disk",
    /* 5 */ "cd/dvd",
    "scanner",
    "optical memory device",
    "medium changer",
    "communications",
    /* 0xa */ "graphics",
    "graphics",
    "storage array controller",
    "enclosure services device",
    "simplified direct access device",
    "optical card reader/writer device",
    /* 0x10 */ "bridging expander",
    "object based storage",
    "automation/driver interface",
};


struct page_code_desc {
        int page_code;
        const char * desc;
};
static struct page_code_desc pc_desc_arr[] = {
        {0x0, "Supported diagnostic pages"},
        {0x1, "Configuration (SES)"},
        {0x2, "Enclosure status/control (SES)"},
        {0x3, "Help text (SES)"},
        {0x4, "String In/Out (SES)"},
        {0x5, "Threshold In/Out (SES)"},
        {0x6, "Array Status/Control (SES, obsolete)"},
        {0x7, "Element descriptor (SES)"},
        {0x8, "Short enclosure status (SES)"},
        {0x9, "Enclosure busy (SES-2)"},
        {0xa, "Device element status (SES-2)"},
        {0xb, "Subenclosure help text (SES-2)"},
        {0xc, "Subenclosure string In/Out (SES-2)"},
        {0xd, "Supported SES diagnostic pages (SES-2)"},
        {0x3f, "Protocol specific SAS (SAS-1)"},
        {0x40, "Translate address (SBC)"},
        {0x41, "Device status (SBC)"},
};

static const char * find_page_code_desc(int page_num)
{
    int k;
    int num = sizeof(pc_desc_arr) / sizeof(pc_desc_arr[0]);
    const struct page_code_desc * pcdp = &pc_desc_arr[0];

    for (k = 0; k < num; ++k, ++pcdp) {
        if (page_num == pcdp->page_code)
            return pcdp->desc;
        else if (page_num < pcdp->page_code)
            return NULL;
    }
    return NULL;
}

struct element_desc {
        int type_code;
        const char * desc;
};
static struct element_desc element_desc_arr[] = {
        {0x0, "Unspecified"},
        {0x1, "Device"},
        {0x2, "Power supply"},
        {0x3, "Cooling"},
        {0x4, "Temperature sense"},
        {0x5, "Door lock"},
        {0x6, "Audible alarm"},
        {0x7, "Enclosure service controller electronics"},
        {0x8, "SCC controller electronics"},
        {0x9, "Nonvolatile cache"},
        {0xa, "Invalid operation reason"},
        {0xb, "Uninterruptible power supply"},
        {0xc, "Display"},
        {0xd, "Key pad entry"},
        {0xe, "Enclosure"},
        {0xf, "SCSI port/transceiver"},
        {0x10, "Language"},
        {0x11, "Communication port"},
        {0x12, "Voltage sensor"},
        {0x13, "Current sensor"},
        {0x14, "SCSI target port"},
        {0x15, "SCSI initiator port"},
        {0x16, "Simple subenclosure"},
        {0x17, "Array device"},
};

static const char * find_element_desc(int type_code)
{
    int k;
    int num = sizeof(element_desc_arr) / sizeof(element_desc_arr[0]);
    const struct element_desc * edp = &element_desc_arr[0];

    for (k = 0; k < num; ++k, ++edp) {
        if (type_code == edp->type_code)
            return edp->desc;
        else if (type_code < edp->type_code)
            return NULL;
    }
    return NULL;
}

struct element_hdr {
    unsigned char etype;
    unsigned char num_elements;
    unsigned char se_id;
    unsigned char unused;
};

static struct element_hdr element_hdr_arr[MX_ELEM_HDR];

static void ses_configuration_sdg(const unsigned char * resp, int resp_len)
{
    int j, k, el, num_subs, sum_elem_types;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;
    const unsigned char * text_ucp;
    const char * cp;

    printf("Configuration diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    num_subs = resp[1] + 1;  /* number of subenclosures (add 1 for primary) */
    sum_elem_types = 0;
    last_ucp = resp + resp_len - 1;
    printf("  number of subenclosures (other than primary): %d\n",
            num_subs - 1);
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    ucp = resp + 8;
    for (k = 0; k < num_subs; ++k, ucp += el) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        el = ucp[3] + 4;
        sum_elem_types += ucp[2];
        printf("    Subenclosure identifier: %d\n", ucp[1]);
        printf("      relative e.s. process id: %d, number of e.s. processes"
               ": %d\n", ((ucp[0] & 0x70) >> 4), (ucp[0] & 0x7));
        printf("      number of element types supported: %d\n", ucp[2]);
        if (el < 40) {
            fprintf(stderr, "      enc descriptor len=%d ??\n", el);
            continue;
        }
        printf("      logical id (hex): ");
        for (j = 0; j < 8; ++j)
            printf("%02x ", ucp[4 + j]);
        printf("\n      vendor: %.8s  product: %.16s  rev: %.4s\n",
               ucp + 12, ucp + 20, ucp + 36);
        if (el > 40) {
            printf("      vendor-specific data:\n");
            dStrHex((const char *)(ucp + 40), el - 40, 0);
        }
    }
    printf("\n");
    text_ucp = ucp + (sum_elem_types * 4);
    for (k = 0; k < sum_elem_types; ++k, ucp += 4) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        cp = find_element_desc(ucp[0]);
        if (cp)
            printf("    Element type: %s, subenclosure id: %d\n",
                   cp, ucp[2]);
        else
            printf("    Element type: [0x%x], subenclosure id: %d\n",
                   ucp[0], ucp[2]);
        printf("      possible number of elements: %d\n", ucp[1]);
        if (ucp[3] > 0) {
            if (text_ucp > last_ucp)
                goto truncated;
            printf("      Description: %.*s\n", ucp[3], text_ucp);
            text_ucp += ucp[3];
        }
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static int populate_element_hdr_arr(int fd, struct element_hdr * ehp,
                                    unsigned int * generationp, int verbose)
{
    int resp_len, k, el, num_subs, sum_elem_types;
    unsigned int gen_code;
    unsigned char resp[MX_ALLOC_LEN];
    int rsp_buff_size = MX_ALLOC_LEN;
    const unsigned char * ucp;
    const unsigned char * last_ucp;

    if (0 == do_rcvdiag(fd, 1, 1, resp, rsp_buff_size, 1, verbose)) {
        resp_len = (resp[2] << 8) + resp[3] + 4;
        if (resp_len > rsp_buff_size) {
            fprintf(stderr, "<<< warning response buffer too small "
                    "[%d but need %d]>>>\n", rsp_buff_size, resp_len);
            resp_len = rsp_buff_size;
        }
        if (1 != resp[0]) {
            if ((0x9 == resp[0]) && (1 & resp[1]))
                printf("Enclosure busy, try again later\n");
            else if (0x8 == resp[0])
                printf("Enclosure only supports Short Enclosure status: "
                       "0x%x\n", resp[1]);
            else
                printf("Invalid response, wanted page code: 0x%x but got "
                       "0x%x\n", 1, resp[0]);
            return -1;
        }
        if (resp_len < 4)
            return -1;
        num_subs = resp[1] + 1;
        sum_elem_types = 0;
        last_ucp = resp + resp_len - 1;
        gen_code = (resp[4] << 24) | (resp[5] << 16) |
                   (resp[6] << 8) | resp[7];
        if (generationp)
            *generationp = gen_code;
        ucp = resp + 8;
        for (k = 0; k < num_subs; ++k, ucp += el) {
            if ((ucp + 4) > last_ucp)
                goto p_truncated;
            el = ucp[3] + 4;
            sum_elem_types += ucp[2];
            if (el < 40) {
                fprintf(stderr, "populate: short enc descriptor len=%d ??\n",
                        el);
                continue;
            }
        }
        for (k = 0; k < sum_elem_types; ++k, ucp += 4) {
            if ((ucp + 4) > last_ucp)
                goto p_truncated;
            if (k >= MX_ELEM_HDR) {
                fprintf(stderr, "populate: too many elements\n");
                return -1;
            }
            ehp[k].etype = ucp[0];
            ehp[k].num_elements = ucp[1];
            ehp[k].se_id = ucp[2];
            ehp[k].unused = 0;
        }
        return sum_elem_types;
    } else {
        fprintf(stderr, "populate: couldn't read config page\n");
        return -1;
    }
p_truncated:
    fprintf(stderr, "populate: config too short\n");
    return -1;
}


static const char * element_status_desc[] = {
    "Unsupported", "OK", "Critical", "Non-critical",
    "Unrecoverable", "Not installed", "Unknown", "Not available",
    "reserved [8]", "reserved [9]", "reserved [10]", "reserved [11]",
    "reserved [12]", "reserved [13]", "reserved [14]", "reserved [15]",
};

static const char * actual_speed_desc[] = {
    "stopped", "at lowest speed", "at second lowest speed",
    "at third lowest speed", "at intermediate speed",
    "at third highest speed", "at second highest speed", "at highest speed"
};

static const char * nv_cache_unit[] = {
    "Bytes", "KiB", "MiB", "GiB"
};

static const char * invop_type_desc[] = {
    "SEND DIAGNOSTIC page code error", "SEND DIAGNOSTIC page format error",
    "Reserved", "Vendor specific error"
};

static void print_over_elem_status(const char * pad,
                                   const unsigned char * statp, int etype,
                                   int filter)
{
    int res;

    printf("%sPredicted failure=%d, swap=%d, status: %s\n",
           pad, !!(statp[0] & 0x40), !!(statp[0] & 0x10),
           element_status_desc[statp[0] & 0xf]);
    switch (etype) { /* element types */
    case 0:     /* unspecified */
        printf("%sstatus in hex: %02x %02x %02x %02x\n",
               pad, statp[0], statp[1], statp[2], statp[3]);
        break;
    case 1:     /* device */
        printf("%sSlot address: %d\n", pad, statp[1]);
        if ((! filter) || (0xe0 & statp[2]))
            printf("%sApp client bypassed A=%d, Do not remove=%d, Enc "
                   "bypassed A=%d\n", pad, !!(statp[2] & 0x80),
                   !!(statp[2] & 0x40), !!(statp[2] & 0x20));
        if ((! filter) || (0x1c & statp[2]))
            printf("%sEnc bypassed B=%d, Ready to insert=%d, RMV=%d, Ident="
                   "%d\n", pad, !!(statp[2] & 0x10), !!(statp[2] & 0x8),
                   !!(statp[2] & 0x4), !!(statp[2] & 0x2));
        if ((! filter) || ((1 & statp[2]) || (0xe0 & statp[3])))
            printf("%sReport=%d, App client bypassed B=%d, Fault sensed=%d, "
                   "Fault requested=%d\n", pad, !!(statp[2] & 0x1),
                   !!(statp[3] & 0x80), !!(statp[3] & 0x40),
                   !!(statp[3] & 0x20));
        if ((! filter) || (0x1e & statp[3]))
            printf("%sDevice off=%d, Bypassed A=%d, Bypassed B=%d, Device "
                   "bypassed A=%d\n", pad, !!(statp[3] & 0x10),
                   !!(statp[3] & 0x8), !!(statp[3] & 0x4), !!(statp[3] & 0x2));
        if ((! filter) || (0x1 & statp[3]))
            printf("%sDevice bypassed B=%d\n", pad, !!(statp[3] & 0x1));
        break;
    case 2:     /* power supply */
        if ((! filter) || ((0x80 & statp[1]) || (0xe & statp[2])))
            printf("%sIdent=%d, DC overvoltage=%d, DC undervoltage=%d, DC "
                   "overcurrent=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[2] & 0x8), !!(statp[2] & 0x4), !!(statp[2] & 0x2));
        if ((! filter) || (0x78 & statp[3]))
            printf("%sFail=%d, Requested on=%d, Off=%d, Overtemperature "
                   "fail=%d\n", pad, !!(statp[3] & 0x40), !!(statp[3] & 0x20),
                   !!(statp[3] & 0x10), !!(statp[3] & 0x8));
        if ((! filter) || (0x7 & statp[3]))
            printf("%sTemperature warn=%d, AC fail=%d, DC fail=%d\n",
                   pad, !!(statp[3] & 0x4), !!(statp[3] & 0x2),
                   !!(statp[3] & 0x1));
        break;
    case 3:     /* cooling */
        if ((! filter) || ((0x80 & statp[1]) || (0x70 & statp[3])))
            printf("%sIdent=%d, Fail=%d, Requested on=%d, Off=%d\n", pad,
                   !!(statp[1] & 0x80), !!(statp[3] & 0x40),
                   !!(statp[3] & 0x20), !!(statp[3] & 0x10));
        printf("%sActual speed=%d rpm, Fan %s\n", pad,
               (((3 & statp[1]) << 8) + statp[2]) * 10,
               actual_speed_desc[7 & statp[3]]);
        break;
    case 4:     /* temperature sensor */
        if ((! filter) || ((0x80 & statp[1]) || (0xf & statp[3])))
            printf("%sIdent=%d, OT Failure=%d, OT warning=%d, UT failure=%d, "
                   "UT warning=%d\n", pad, !!(statp[1] & 0x80), 
                   !!(statp[3] & 0x8), !!(statp[3] & 0x4), !!(statp[3] & 0x2),
                   !!(statp[3] & 0x1));
        if (statp[2])
            printf("%sTemperature=%d C\n", pad, (int)statp[2] - 20);
        else
            printf("%sTemperature: <reserved>\n", pad);
        break;
    case 5:     /* door lock */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[3])))
            printf("%sIdent=%d, Unlock=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[3] & 0x1));
        break;
    case 6:     /* audible alarm */
        if ((! filter) || ((0x80 & statp[1]) || (0xd0 & statp[3])))
            printf("%sIdent=%d, Request mute=%d, Mute=%d, Remind=%d\n", pad,
                   !!(statp[1] & 0x80), !!(statp[3] & 0x80),
                   !!(statp[3] & 0x40), !!(statp[3] & 0x10));
        if ((! filter) || (0xf & statp[3]))
            printf("%sTone indicator: Info=%d, Non-crit=%d, Crit=%d, "
                   "Unrecov=%d\n", pad, !!(statp[3] & 0x8), !!(statp[3] & 0x4),
                   !!(statp[3] & 0x2), !!(statp[3] & 0x1));
        break;
    case 7:     /* enclosure services controller electronics element */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[2])))
            printf("%sIdent=%d, Report=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[2] & 0x1));
        break;
    case 8:     /* SCC controller electronics element */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[2])))
            printf("%sIdent=%d, Report=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[2] & 0x1));
        break;
    case 9:     /* Non volatile cache element */
        res = (statp[2] << 8) + statp[3];
        printf("%sIdent=%d, Size multiplier=%d, Non volatile cache "
               "size=0x%x\n", pad, !!(statp[1] & 0x80),
               (statp[1] & 0x3), res);
        printf("%sHence non volatile cache size: %d %s\n", pad, res,
               nv_cache_unit[statp[1] & 0x3]);
        break;
    case 0xa:   /* Invalid operation reason element */
        res = ((statp[1] >> 6) & 3);
        printf("%sInvop type=%d   %s\n", pad, res, invop_type_desc[res]);
        switch (res) {
        case 0:
            printf("%sPage not supported=%d\n", pad, (statp[1] & 1));
            break;
        case 1:
            printf("%sByte offset=%d, bit number=%d\n", pad,
                   (statp[2] << 8) + statp[3], (statp[1] & 7));
            break;
        case 2:
        case 3:
            printf("%slast 3 bytes (hex): %02x %02x %02x\n", pad, statp[1],
                   statp[2], statp[3]);
            break;
        default:
            break;
        }
        break;
    case 0xb:   /* Uninterruptible power supply */
        if (0 == statp[1])
            printf("%sBattery status: discharged or unknown\n", pad);
        else if (255 == statp[1])
            printf("%sBattery status: 255 or more minutes remaining\n", pad);
        else
            printf("%sBattery status: %d minutes remaining\n", pad, statp[1]);
        if ((! filter) || (0xf8 & statp[2]))
            printf("%sAC low=%d, AC high=%d, AC qual=%d, AC fail=%d, DC fail="
                   "%d\n", pad, !!(statp[2] & 0x80), !!(statp[2] & 0x40),
                   !!(statp[2] & 0x20), !!(statp[2] & 0x10),
                   !!(statp[2] & 0x8));
        if ((! filter) || ((0x7 & statp[2]) || (0x83 & statp[3])))
            printf("%sUPS fail=%d, Warn=%d, Intf fail=%d, Ident=%d, Batt fail"
                   "=%d,BPF=%d\n", pad, !!(statp[2] & 0x4), !!(statp[2] & 0x2),
                   !!(statp[2] & 0x1), !!(statp[3] & 0x80), !!(statp[3] & 0x2),
                   !!(statp[3] & 0x1));
        break;
    case 0xc:   /* Display */
        if ((! filter) || (0x80 & statp[1]))
            printf("%sIdent=%d\n", pad, !!(statp[1] & 0x80));
        break;
    case 0xd:   /* Key pad entry */
        if ((! filter) || (0x80 & statp[1]))
            printf("%sIdent=%d\n", pad, !!(statp[1] & 0x80));
        break;
    case 0xe:   /* Enclosure */
        if ((! filter) || ((0x80 & statp[1]) || (0x3 & statp[2])))
            printf("%sIdent=%d, Failure indication=%d, Warning indication="
                   "%d\n", pad, !!(statp[1] & 0x80), !!(statp[2] & 0x2),
                   !!(statp[2] & 0x1));
        if ((! filter) || (0x3 & statp[3]))
            printf("%sFailure requested=%d, Warning requested=%d\n",
                   pad, !!(statp[3] & 0x2), !!(statp[3] & 0x1));
        break;
    case 0xf:   /* SCSI port/transceiver */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[2]) ||
                           (0x13 & statp[3])))
            printf("%sIdent=%d, Report=%d, disabled=%d, loss of link=%d, Xmit"
                   " fail=%d\n", pad, !!(statp[1] & 0x80), !!(statp[2] & 0x1),
                   !!(statp[3] & 0x10), !!(statp[3] & 0x2),
                   !!(statp[3] & 0x1));
        break;
    case 0x10:   /* Language */
        printf("%sIdent=%d, Language code: %.2s\n", pad, !!(statp[1] & 0x80),
               statp + 2);
        break;
    case 0x11:   /* Communication port */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[3])))
            printf("%sIdent=%d, Disabled=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[3] & 0x1));
        break;
    case 0x12:   /* Voltage sensor */
        if ((! filter) || (0x8f & statp[1]))
            printf("%sIdent=%d, Warn Over=%d, Warn Under=%d, Crit Over=%d, "
                   "Crit Under=%d\n", pad, !!(statp[1] & 0x80),
                   !!(statp[1] & 0x8), !!(statp[1] & 0x4),!!(statp[1] & 0x2),
                   !!(statp[1] & 0x1));
        printf("%sVoltage: %.2f volts\n", pad,
               ((int)(short)((statp[2] << 8) + statp[3]) / 100.0));
        break;
    case 0x13:   /* Current sensor */
        if ((! filter) || (0x8a & statp[1]))
            printf("%sIdent=%d, Warn Over=%d, Crit Over=%d\n", pad,
                   !!(statp[1] & 0x80), !!(statp[1] & 0x8),
                   !!(statp[1] & 0x2));
        printf("%sCurrent: %.2f amps\n", pad,
               ((int)(short)((statp[2] << 8) + statp[3]) / 100.0));
        break;
    case 0x14:   /* SCSI target port */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[2]) ||
                           (0x1 & statp[3])))
            printf("%sIdent=%d, Report=%d, Enabled=%d\n", pad,
                   !!(statp[1] & 0x80), !!(statp[2] & 0x1),
                   !!(statp[3] & 0x1));
        break;
    case 0x15:   /* SCSI initiator port */
        if ((! filter) || ((0x80 & statp[1]) || (0x1 & statp[2]) ||
                           (0x1 & statp[3])))
            printf("%sIdent=%d, Report=%d, Enabled=%d\n", pad,
                   !!(statp[1] & 0x80), !!(statp[2] & 0x1),
                   !!(statp[3] & 0x1));
        break;
    case 0x16:   /* Simple subenclosure */
        printf("%sIdent=%d, Short enclosure status: 0x%x\n", pad,
               !!(statp[1] & 0x80), statp[3]);
        break;
    case 0x17:   /* Array device */
        if ((! filter) || (0xf0 & statp[1]))
            printf("%sOK=%d, Reserved device=%d, Hot spare=%d, Cons check="
                   "%d\n", pad, !!(statp[1] & 0x80), !!(statp[1] & 0x40),
                   !!(statp[1] & 0x20), !!(statp[1] & 0x10));
        if ((! filter) || (0xf & statp[1]))
            printf("%sIn crit array=%d, In failed array=%d, Rebuild/remap=%d"
                   ", R/R abort=%d\n", pad, !!(statp[1] & 0x8),
                   !!(statp[1] & 0x4), !!(statp[1] & 0x2),
                   !!(statp[1] & 0x1));
        if ((! filter) || (0xf0 & statp[2]))
            printf("%sApp client bypass A=%d, Don't remove=%d, Enc bypass "
                   "A=%d, Enc bypass B=%d\n", pad, !!(statp[2] & 0x80),
                   !!(statp[2] & 0x40), !!(statp[2] & 0x20),
                   !!(statp[2] & 0x10));
        if ((! filter) || (0xf & statp[2]))
            printf("%sReady to insert=%d, RMV=%d, Ident=%d, Report=%d\n",
                   pad, !!(statp[2] & 0x8), !!(statp[2] & 0x4),
                   !!(statp[2] & 0x2), !!(statp[2] & 0x1));
        if ((! filter) || (0xf0 & statp[3]))
            printf("%sApp client bypass B=%d, Fault sensed=%d, Fault reqstd="
                   "%d, Device off=%d\n", pad, !!(statp[3] & 0x80),
                   !!(statp[3] & 0x40), !!(statp[3] & 0x20),
                   !!(statp[3] & 0x10));
        if ((! filter) || (0xf & statp[3]))
            printf("%sBypassed A=%d, Bypassed B=%d, Dev bypassed A=%d, "
                   "Dev bypassed B=%d\n",
                   pad, !!(statp[3] & 0x8), !!(statp[3] & 0x4),
                   !!(statp[3] & 0x2), !!(statp[3] & 0x1));
        break;
    default:
        printf("%sUnknown element type, status in hex: %02x %02x %02x %02x\n",
               pad, statp[0], statp[1], statp[2], statp[3]);
        break;
    }
}

static void ses_enclosure_sdg(const struct element_hdr * ehp, int num_telems,
                              unsigned int ref_gen_code, 
                              const unsigned char * resp, int resp_len,
                              int inner_hex, int filter)
{
    int j, k;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;
    const char * cp;

    printf("Enclosure status diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    printf("  INVOP=%d, INFO=%d, NON-CRIT=%d, CRIT=%d, UNRECOV=%d\n",
           !!(resp[1] & 0x10), !!(resp[1] & 0x8), !!(resp[1] & 0x4),
           !!(resp[1] & 0x2), !!(resp[1] & 0x1));
    last_ucp = resp + resp_len - 1;
    if (resp_len < 8)
        goto truncated;
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    if (ref_gen_code != gen_code) {
        fprintf(stderr, "  <<state of enclosure changed, please try "
                "again>>\n");
        return;
    }
    ucp = resp + 8;
    for (k = 0; k < num_telems; ++k) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        cp = find_element_desc(ehp[k].etype);
        if (cp)
            printf("    Element type: %s, subenclosure id: %d\n",
                   cp, ehp[k].se_id);
        else
            printf("    Element type: [0x%x], subenclosure id: %d\n",
                   ehp[k].etype, ehp[k].se_id);
        if (inner_hex)
            printf("    Overall status(hex): %02x %02x %02x %02x\n", ucp[0],
                   ucp[1], ucp[2], ucp[3]);
        else {
            printf("    Overall status:\n");
            print_over_elem_status("     ", ucp, ehp[k].etype, filter);
        }
        for (ucp += 4, j = 0; j < ehp[k].num_elements; ++j, ucp += 4) {
            if (inner_hex)
                printf("      Element %d status(hex): %02x %02x %02x %02x\n",
                       j + 1, ucp[0], ucp[1], ucp[2], ucp[3]);
            else {
                printf("      Element %d status:\n", j + 1);
                print_over_elem_status("       ", ucp, ehp[k].etype, filter);
            }
        }
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static char * reserved_or_num(char * buff, int buff_len, int num,
                              int reserve_num)
{
    if (num == reserve_num)
        strncpy(buff, "<res>", buff_len);
    else
        snprintf(buff, buff_len, "%d", num);
    if (buff_len > 0)
        buff[buff_len - 1] = '\0';
    return buff;
}

static void ses_threshold_helper(const char * pad, const unsigned char *tp,
                                 int etype, int p_num, int inner_hex)
{
    char buff[128];
    char b[128];
    char b2[128];

    if (p_num < 0)
        strcpy (buff, "Overall threshold");
    else
        snprintf(buff, sizeof(buff) - 1, "Element %d threshold", p_num + 1);
    if (inner_hex) {
        printf("%s%s (in hex): %02x %02x %02x %02x\n", pad, buff,
               tp[0], tp[1], tp[2], tp[3]);
        return;
    }
    switch (etype) {
    case 0x4:  /*temperature */
        printf("%s%s: high critical=%s, high warning=%s\n",
               pad, buff, reserved_or_num(b, 128, tp[0] - 20, -20),
               reserved_or_num(b2, 128, tp[1] - 20, -20));
        printf("%s  low warning=%s, low critical=%s (in degrees Celsius)\n", pad,
               reserved_or_num(b, 128, tp[2] - 20, -20),
               reserved_or_num(b2, 128, tp[3] - 20, -20));
        break;
    case 0xb:  /* UPS */
        if (0 == tp[2])
            strcpy(b, "<vendor>");
        else
            snprintf(b, sizeof(b), "%d", tp[2]);
        printf("%s%s: low warning=%s, ", pad, buff, b);
        if (0 == tp[3])
            strcpy(b, "<vendor>");
        else
            snprintf(b, sizeof(b), "%d", tp[3]);
        printf("low critical=%s (in minutes)\n", b);
        break;
    case 0x12: /* voltage */
        printf("%s%s: high critical=%.1f %%, high warning=%.1f %%\n", pad,
               buff, 0.5 * tp[0], 0.5 * tp[1]);
        printf("%s  low warning=%.1f %%, low critical=%.1f %% (from nominal "
               "voltage)\n", pad, 0.5 * tp[2], 0.5 * tp[3]);
        break;
    case 0x13: /* current */
        printf("%s%s: high critical=%.1f %%, high warning=%.1f %%\n", pad,
               buff, 0.5 * tp[0], 0.5 * tp[1]);
        printf("%s  (above nominal current)\n", pad);
        break;
    default:
        break;
    }
}

static void ses_threshold_sdg(const struct element_hdr * ehp, int num_telems,
                              unsigned int ref_gen_code, 
                              const unsigned char * resp, int resp_len,
                              int inner_hex)
{
    int j, k;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;
    const char * cp;

    printf("Threshold In diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    printf("  INVOP=%d\n", !!(resp[1] & 0x10));
    last_ucp = resp + resp_len - 1;
    if (resp_len < 8)
        goto truncated;
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    if (ref_gen_code != gen_code) {
        fprintf(stderr, "  <<state of enclosure changed, please try "
                "again>>\n");
        return;
    }
    ucp = resp + 8;
    for (k = 0; k < num_telems; ++k) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        cp = find_element_desc(ehp[k].etype);
        if (cp)
            printf("    Element type: %s, subenclosure id: %d\n",
                   cp, ehp[k].se_id);
        else
            printf("    Element type: [0x%x], subenclosure id: %d\n",
                   ehp[k].etype, ehp[k].se_id);
        ses_threshold_helper("    ", ucp, ehp[k].etype, -1, inner_hex);
        for (ucp += 4, j = 0; j < ehp[k].num_elements; ++j, ucp += 4) {
            ses_threshold_helper("      ", ucp, ehp[k].etype, j, inner_hex);
        }
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static void ses_element_desc_sdg(const struct element_hdr * ehp,
                         int num_telems, unsigned int ref_gen_code,
                         const unsigned char * resp, int resp_len)
{
    int j, k, desc_len;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;
    const char * cp;

    printf("Element descriptor In diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    last_ucp = resp + resp_len - 1;
    if (resp_len < 8)
        goto truncated;
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    if (ref_gen_code != gen_code) {
        fprintf(stderr, "  <<state of enclosure changed, please try "
                "again>>\n");
        return;
    }
    ucp = resp + 8;
    for (k = 0; k < num_telems; ++k) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        cp = find_element_desc(ehp[k].etype);
        if (cp)
            printf("    Element type: %s, subenclosure id: %d\n",
                   cp, ehp[k].se_id);
        else
            printf("    Element type: [0x%x], subenclosure id: %d\n",
                   ehp[k].etype, ehp[k].se_id);
        desc_len = (ucp[2] << 8) + ucp[3] + 4;
        if (desc_len > 4)
            printf("    Overall descriptor: %.*s\n", desc_len - 4, ucp + 4);
        else
            printf("    Overall descriptor: <empty>\n");
        for (ucp += desc_len, j = 0; j < ehp[k].num_elements; ++j, ucp += desc_len) {
            desc_len = (ucp[2] << 8) + ucp[3] + 4;
            if (desc_len > 4)
                printf("      Element %d descriptor: %.*s\n", j + 1,
                       desc_len - 4, ucp + 4);
            else
                printf("      Element %d descriptor: <empty>\n", j + 1);
        }
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static const char * transport_proto_arr[] = {
    "Fibre Channel (FCP-2)",
    "Parallel SCSI (SPI-5)",
    "SSA (SSA-S3P)",
    "IEEE 1394 (SBP-3)",
    "Remote Direct Memory Access (RDMA)",
    "Internet SCSI (iSCSI)",
    "Serial Attached SCSI (SAS)",
    "Automation/Drive Interface Transport Protocol (ADT)",
    "ATA Packet Interface (ATA/ATAPI-7)",
    "Ox9", "Oxa", "Oxb", "Oxc", "Oxd", "Oxe",
    "No specific protocol"
};

static char * sas_device_type[] = {
    "no device attached",
    "end device",
    "edge expander device",
    "fanout expander device",
    "reserved [4]", "reserved [5]", "reserved [6]", "reserved [7]"
};

static void ses_transport_proto(const unsigned char * ucp, int len,
                                int elem_num)
{
    int ports, phys, j, m;
    const unsigned char * per_ucp;

    switch (0xf & ucp[0]) {
    case 0:
        ports = ucp[2];
        printf("   [%d] Transport protocol: FCP, number of ports: %d\n",
               elem_num + 1, ports);
        printf("    node_name: ");
        for (m = 0; m < 8; ++m)
            printf("%02x", ucp[4 + m]);
        printf("\n");
        per_ucp = ucp + 12;
        for (j = 0; j < ports; ++j, per_ucp += 16) {
            printf("    [%d] port loop position: %d, port requested hard "
                   "address: %d\n", j + 1, per_ucp[0], per_ucp[4]);
            printf("      n_port identifier: %02x%02x%02x\n",
                   per_ucp[5], per_ucp[6], per_ucp[7]);
            printf("      n_port name: ");
            for (m = 0; m < 8; ++m)
                printf("%02x", per_ucp[8 + m]);
            printf("\n");
        }
        break;
    case 6:
        phys = ucp[2];
        printf("   [%d] Transport protocol: SAS, number of phys: %d\n",
               elem_num + 1, phys);
        printf("    not all phys: %d\n", ucp[3] & 1);
        per_ucp = ucp + 4;
        for (j = 0; j < phys; ++j, per_ucp += 28) {
            printf("    [%d] device type: %s\n", phys + 1,
                   sas_device_type[(0x70 & per_ucp[4]) >> 4]);
            printf("      initiator port for: %s %s %s\n",
                   ((per_ucp[6] & 8) ? "SSP" : ""),
                   ((per_ucp[6] & 4) ? "STP" : ""),
                   ((per_ucp[6] & 2) ? "SMP" : ""));
            printf("      target port for: %s %s %s\n",
                   ((per_ucp[7] & 8) ? "SSP" : ""),
                   ((per_ucp[7] & 4) ? "STP" : ""),
                   ((per_ucp[7] & 2) ? "SMP" : ""));
            printf("      attached SAS address: ");
            for (m = 0; m < 8; ++m)
                printf("%02x", per_ucp[8 + m]);
            printf("\n      SAS address: ");
            for (m = 0; m < 8; ++m)
                printf("%02x", per_ucp[16 + m]);
            printf("\n      phy identifier: 0x%x\n", per_ucp[24]);
        }
        break;
    default:
        printf("   [%d] Transport protocol: %s not decoded, in hex:\n",
               elem_num + 1, transport_proto_arr[0xf & ucp[0]]);
        dStrHex((const char *)ucp + 4, len - 4, 0);
        break;
    }
}

static void ses_device_elem_sdg(const struct element_hdr * ehp,
                         int num_telems, unsigned int ref_gen_code,
                         const unsigned char * resp, int resp_len)
{
    int j, k, desc_len;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;
    const char * cp;

    printf("Device element status diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    last_ucp = resp + resp_len - 1;
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    if (ref_gen_code != gen_code) {
        fprintf(stderr, "  <<state of enclosure changed, please try "
                "again>>\n");
        return;
    }
    ucp = resp + 8;
    for (k = 0; k < num_telems; ++k) {
        if ((ucp + 2) > last_ucp)
            goto truncated;
        if ((1 != ehp[k].etype) && (0x17 != ehp[k].etype))
            continue;
        /* only interested in device and array elements */
        cp = find_element_desc(ehp[k].etype);
        if (cp)
            printf("  Element type: %s, subenclosure id: %d\n",
                   cp, ehp[k].se_id);
        else
            printf("  Element type: [0x%x], subenclosure id: %d\n",
                   ehp[k].etype, ehp[k].se_id);
        for (j = 0; j < ehp[k].num_elements; ++j, ucp += desc_len) {
            desc_len = ucp[1] + 2;
            ses_transport_proto(ucp, desc_len, j);
        }
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static void ses_subenc_help_sdg(const unsigned char * resp, int resp_len)
{
    int k, el, num_subs;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;

    printf("Subenclosure help text diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    num_subs = resp[1] + 1;  /* number of subenclosures (add 1 for primary) */
    last_ucp = resp + resp_len - 1;
    printf("  number of subenclosures (other than primary): %d\n",
            num_subs - 1);
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    ucp = resp + 8;
    for (k = 0; k < num_subs; ++k, ucp += el) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        el = (ucp[2] << 8) + ucp[3] + 4;
        printf("   subenclosure identifier: %d\n", ucp[1]);
        if (el > 4)
            printf("    %.*s\n", el - 4, ucp + 4);
        else
            printf("    <empty>\n");
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static void ses_subenc_string_sdg(const unsigned char * resp, int resp_len)
{
    int k, el, num_subs;
    unsigned int gen_code;
    const unsigned char * ucp;
    const unsigned char * last_ucp;

    printf("Subenclosure string in diagnostic page:\n");
    if (resp_len < 4)
        goto truncated;
    num_subs = resp[1] + 1;  /* number of subenclosures (add 1 for primary) */
    last_ucp = resp + resp_len - 1;
    printf("  number of subenclosures (other than primary): %d\n",
            num_subs - 1);
    gen_code = (resp[4] << 24) | (resp[5] << 16) |
               (resp[6] << 8) | resp[7];
    printf("  generation code: 0x%x\n", gen_code);
    ucp = resp + 8;
    for (k = 0; k < num_subs; ++k, ucp += el) {
        if ((ucp + 4) > last_ucp)
            goto truncated;
        el = (ucp[2] << 8) + ucp[3] + 4;
        printf("   subenclosure identifier: %d\n", ucp[1]);
        if (el > 4)
            dStrHex((const char *)(ucp + 4), el - 4, 0);
        else
            printf("    <empty>\n");
    }
    return;
truncated:
    fprintf(stderr, "    <<<response too short>>>\n");
    return;
}

static void ses_supported_pages_sdg(const char * leadin,
                                    const unsigned char * resp, int resp_len)
{
    int k, code, prev;
    const char * cp;

    printf("%s:\n", leadin);
    for (k = 0, prev = 0; k < (resp_len - 4); ++k, prev = code) {
        code = resp[k + 4];
        if (code < prev)
            break;      /* assume to be padding at end */
        cp = find_page_code_desc(code);
        printf("  %s [0x%x]\n", (cp ? cp : "<unknown>"), code);
    }
}

static int read_hex(const char * inp, unsigned char * arr, int * arr_len)
{
    int in_len, k, j, m, off;
    unsigned int h;
    const char * lcp;
    char * cp;
    char line[512];

    if ((NULL == inp) || (NULL == arr) || (NULL == arr_len))
        return 1;
    lcp = inp;
    in_len = strlen(inp);
    if (0 == in_len) {
        *arr_len = 0;
    }
    if ('-' == inp[0]) {        /* read from stdin */
        for (j = 0, off = 0; j < 512; ++j) {
            if (NULL == fgets(line, sizeof(line), stdin))
                break;
            in_len = strlen(line);
            if (in_len > 0) {
                if ('\n' == line[in_len - 1]) {
                    --in_len;
                    line[in_len] = '\0';
                }
            }
            if (0 == in_len)
                continue;
            lcp = line;
            m = strspn(lcp, " \t");
            if (m == in_len)
                continue;
            lcp += m;
            in_len -= m;
            if ('#' == *lcp)
                continue;
            k = strspn(lcp, "0123456789aAbBcCdDeEfF ,\t");
            if (in_len != k) {
                fprintf(stderr, "read_hex: syntax error at "
                        "line %d, pos %d\n", j + 1, m + k + 1);
                return 1;
            }
            for (k = 0; k < 1024; ++k) {
                if (1 == sscanf(lcp, "%x", &h)) {
                    if (h > 0xff) {
                        fprintf(stderr, "read_hex: hex number "
                                "larger than 0xff in line %d, pos %d\n",
                                j + 1, (int)(lcp - line + 1));
                        return 1;
                    }
                    arr[off + k] = h;
                    lcp = strpbrk(lcp, " ,\t");
                    if (NULL == lcp) 
                        break;
                    lcp += strspn(lcp, " ,\t");
                    if ('\0' == *lcp)
                        break;
                } else {
                    fprintf(stderr, "read_hex: error in "
                            "line %d, at pos %d\n", j + 1,
                            (int)(lcp - line + 1));
                    return 1;
                }
            }
            off += k + 1;
        }
        *arr_len = off;
    } else {        /* hex string on command line */
        k = strspn(inp, "0123456789aAbBcCdDeEfF,");
        if (in_len != k) {
            fprintf(stderr, "read_hex: error at pos %d\n",
                    k + 1);
            return 1;
        }
        for (k = 0; k < 1024; ++k) {
            if (1 == sscanf(lcp, "%x", &h)) {
                if (h > 0xff) {
                    fprintf(stderr, "read_hex: hex number larger "
                            "than 0xff at pos %d\n", (int)(lcp - inp + 1));
                    return 1;
                }
                arr[k] = h;
                cp = strchr(lcp, ',');
                if (NULL == cp)
                    break;
                lcp = cp + 1;
            } else {
                fprintf(stderr, "read_hex: error at pos %d\n",
                        (int)(lcp - inp + 1));
                return 1;
            }
        }
        *arr_len = k + 1;
    }
    return 0;
}

static void ses_process_status(int sg_fd, int page_code, int do_raw,
                               int do_hex, int inner_hex, int filter,
                               int verbose)
{
    int rsp_len, res;
    unsigned int ref_gen_code;
    unsigned char rsp_buff[MX_ALLOC_LEN];
    int rsp_buff_size = MX_ALLOC_LEN;
    const char * cp;

    memset(rsp_buff, 0, rsp_buff_size);
    if (0 == do_rcvdiag(sg_fd, 1, page_code, rsp_buff, rsp_buff_size, 1,
                        verbose)) {
        rsp_len = (rsp_buff[2] << 8) + rsp_buff[3] + 4;
        if (rsp_len > rsp_buff_size) {
            fprintf(stderr, "<<< warning response buffer too small "
                    "[%d but need %d]>>>\n", rsp_buff_size, rsp_len);
            rsp_len = rsp_buff_size;
        }
        cp = find_page_code_desc(page_code);
        if (page_code != rsp_buff[0]) {
            if ((0x9 == rsp_buff[0]) && (1 & rsp_buff[1])) {
                fprintf(stderr, "Enclosure busy, try again later\n");
                if (do_hex)
                    dStrHex((const char *)rsp_buff, rsp_len, 0);
            } else if (0x8 == rsp_buff[0]) {
                fprintf(stderr, "Enclosure only supports Short Enclosure "
                        "status: 0x%x\n", rsp_buff[1]);
            } else {
                fprintf(stderr, "Invalid response, wanted page code: 0x%x "
                        "but got 0x%x\n", page_code, rsp_buff[0]);
                dStrHex((const char *)rsp_buff, rsp_len, 0);
            }
        } else if (do_raw)
            dStrHex((const char *)rsp_buff + 4, rsp_len - 4, -1);
        else if (do_hex) {
            if (cp)
                printf("Response in hex from diagnostic page: %s\n", cp);
            else
                printf("Response in hex from unknown diagnostic page "
                       "[0x%x]\n", page_code);
            dStrHex((const char *)rsp_buff, rsp_len, 0);
        } else {
            switch (page_code) {
            case 0: 
                ses_supported_pages_sdg("Supported diagnostic pages",
                                        rsp_buff, rsp_len);
                break;
            case 1: 
                ses_configuration_sdg(rsp_buff, rsp_len);
                break;
            case 2: 
                res = populate_element_hdr_arr(sg_fd, element_hdr_arr,
                                               &ref_gen_code, verbose);
                if (res < 0)
                    break;
                ses_enclosure_sdg(element_hdr_arr, res, ref_gen_code,
                                  rsp_buff, rsp_len, inner_hex, filter);
                break;
            case 3: 
                printf("Help text diagnostic page (for primary "
                       "subenclosure):\n");
                if (rsp_len > 4)
                    printf("  %.*s\n", rsp_len - 4, rsp_buff + 4);
                else
                    printf("  <empty>\n");
                break;
            case 4: 
                printf("String In diagnostic page (for primary "
                       "subenclosure):\n");
                if (rsp_len > 4)
                    dStrHex((const char *)(rsp_buff + 4), rsp_len - 4, 0);
                else
                    printf("  <empty>\n");
                break;
            case 5: 
                res = populate_element_hdr_arr(sg_fd, element_hdr_arr,
                                               &ref_gen_code, verbose);
                if (res < 0)
                    break;
                ses_threshold_sdg(element_hdr_arr, res, ref_gen_code,
                                  rsp_buff, rsp_len, inner_hex);
                break;
            case 7: 
                res = populate_element_hdr_arr(sg_fd, element_hdr_arr,
                                               &ref_gen_code, verbose);
                if (res < 0)
                    break;
                ses_element_desc_sdg(element_hdr_arr, res, ref_gen_code,
                                     rsp_buff, rsp_len);
                break;
            case 8: 
                printf("Short enclosure status diagnostic page, "
                       "status=0x%x\n", rsp_buff[1]);
                break;
            case 9: 
                printf("Enclosure busy diagnostic page, "
                       "busy=%d [vendor specific=0x%x]\n",
                       rsp_buff[1] & 1, (rsp_buff[1] >> 1) & 0xff);
                break;
            case 0xa: 
                res = populate_element_hdr_arr(sg_fd, element_hdr_arr,
                                               &ref_gen_code, verbose);
                if (res < 0)
                    break;
                ses_device_elem_sdg(element_hdr_arr, res, ref_gen_code,
                                    rsp_buff, rsp_len);
                break;
            case 0xb: 
                ses_subenc_help_sdg(rsp_buff, rsp_len);
                break;
            case 0xc: 
                ses_subenc_string_sdg(rsp_buff, rsp_len);
                break;
            case 0xd: 
                ses_supported_pages_sdg("Supported SES diagnostic pages",
                                        rsp_buff, rsp_len);
                break;
            default:
                printf("Cannot decode response from diagnostic "
                       "page: %s\n", (cp ? cp : "<unknown>"));
                dStrHex((const char *)rsp_buff, rsp_len, 0);
            }
        }
    } else
        printf("Attempt to fetch status diagnostic page failed\n");
}


int main(int argc, char * argv[])
{
    int sg_fd, res, c;
    int do_control = 0;
    int do_data = 0;
    int do_filter = 0;
    int do_hex = 0;
    int do_raw = 0;
    int do_list = 0;
    int do_status = 0;
    int page_code = 0;
    int verbose = 0;
    int inner_hex = 0;
    int byte1 = 0;
    char device_name[256];
    unsigned char data_arr[1024];
    int arr_len = 0;
    int pd_type = 0;
    int ret = 1;
    struct sg_simple_inquiry_resp inq_resp;

    memset(device_name, 0, sizeof device_name);
    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "b:cd:fhHilp:rsvV", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'b':
            byte1 = sg_get_num(optarg);
            if ((byte1 < 0) || (byte1 > 255)) {
                fprintf(stderr, "bad argument to '--byte1' (0 to 255 "
                        "inclusive)\n");
                return 1;
            }
            break;
        case 'c':
            do_control = 1;
            break;
        case 'd':
            memset(data_arr, 0, sizeof(data_arr));
            if (read_hex(optarg, data_arr + 4, &arr_len)) {
                fprintf(stderr, "bad argument to '--data'\n");
                return 1;
            }
            do_data = 1;
            break;
        case 'f':
            do_filter = 1;
            break;
        case 'h':
        case '?':
            usage();
            return 0;
        case 'H':
            ++do_hex;
            break;
        case 'i':
            inner_hex = 1;
            break;
        case 'l':
            do_list = 1;
            break;
        case 'p':
            page_code = sg_get_num(optarg);
            if ((page_code < 0) || (page_code > 255)) {
                fprintf(stderr, "bad argument to '--page' (0 to 255 "
                        "inclusive)\n");
                return 1;
            }
            break;
        case 'r':
            do_raw = 1;
            break;
        case 's':
            do_status = 1;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            fprintf(stderr, ME "version: %s\n", version_str);
            return 0;
        default:
            fprintf(stderr, "unrecognised switch code 0x%x ??\n", c);
            usage();
            return 1;
        }
    }
    if (optind < argc) {
        if ('\0' == device_name[0]) {
            strncpy(device_name, argv[optind], sizeof(device_name) - 1);
            device_name[sizeof(device_name) - 1] = '\0';
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                fprintf(stderr, "Unexpected extra argument: %s\n",
                        argv[optind]);
            usage();
            return 1;
        }
    }
    if (do_list) {
        int k;
        int num = sizeof(pc_desc_arr) / sizeof(pc_desc_arr[0]);
        const struct page_code_desc * pcdp = &pc_desc_arr[0];
        const struct element_desc * edp = &element_desc_arr[0];

    
        printf("Known diagnostic pages (followed by page code):\n");
        for (k = 0; k < num; ++k, ++pcdp)
            printf("    %s  [0x%x]\n", pcdp->desc, pcdp->page_code);
        printf("\nKnown SES element type names (followed by element type "
               "code):\n");
        num = sizeof(element_desc_arr) / sizeof(element_desc_arr[0]);
        for (k = 0; k < num; ++k, ++edp)
            printf("    %s  [0x%x]\n", edp->desc, edp->type_code);
        return 0;
    }
    if (do_control && do_status) {
        fprintf(stderr, "cannot have both '--control' and '--status'\n");
        usage();
        return 1;
    } else if (1 == do_control) {
        if (! do_data) {
            fprintf(stderr, "need to give '--data' in control mode\n");
            usage();
            return 1;
        }
    } else if (0 == do_status)
        do_status = 1;  /* default to receiving status pages */

    if (0 == device_name[0]) {
        fprintf(stderr, "missing device name!\n");
        usage();
        return 1;
    }

    sg_fd = open(device_name, O_RDWR);
    if (sg_fd < 0) {
        perror(ME "open error");
        return 1;
    }
    if (! do_raw) {
        if (sg_simple_inquiry(sg_fd, &inq_resp, 1, verbose)) {
            fprintf(stderr, ME "%s doesn't respond to a SCSI INQUIRY\n",
                    device_name);
            goto err_out;
        } else {
            printf("  %.8s  %.16s  %.4s\n", inq_resp.vendor,
                   inq_resp.product, inq_resp.revision);
            pd_type = inq_resp.peripheral_type;
            if (0xd == pd_type)
                printf("    enclosure services device\n");
            else if (0x40 & inq_resp.byte_6)
                printf("    %s device has EncServ bit set\n",
                       scsi_ptype_strs[pd_type]);
            else
                printf("    %s device (not an enclosure)\n",
                       scsi_ptype_strs[pd_type]);
        }
    }
    if (do_status)
        ses_process_status(sg_fd, page_code, do_raw, do_hex, inner_hex,
                           do_filter, verbose);
    else { /* control page requested */
        data_arr[0] = page_code;
        data_arr[1] = byte1;
        data_arr[2] = (arr_len >> 8) & 0xff;
        data_arr[3] = arr_len & 0xff;
        switch (page_code) {
        case 0x2:       /* Enclosure control diagnostic page */
            printf("Sending Enclosure control [0x%x] page, with page "
                   "length=%d bytes\n", page_code, arr_len);
            if (do_senddiag(sg_fd, 1, data_arr, arr_len + 4, 1, verbose)) {
                fprintf(stderr, "couldn't send Enclosure control page\n");
                goto err_out;
            }
            break;
        case 0x4:       /* String Out diagnostic page */
            printf("Sending String Out [0x%x] page, with page length=%d "
                   "bytes\n", page_code, arr_len);
            if (do_senddiag(sg_fd, 1, data_arr, arr_len + 4, 1, verbose)) {
                fprintf(stderr, "couldn't send String Out page\n");
                goto err_out;
            }
            break;
        case 0x5:       /* Threshold Out diagnostic page */
            printf("Sending Threshold Out [0x%x] page, with page length=%d "
                   "bytes\n", page_code, arr_len);
            if (do_senddiag(sg_fd, 1, data_arr, arr_len + 4, 1, verbose)) {
                fprintf(stderr, "couldn't send Threshold Out page\n");
                goto err_out;
            }
            break;
        case 0x6:       /* Array control diagnostic page (obsolete) */
            printf("Sending Array control [0x%x] page, with page "
                   "length=%d bytes\n", page_code, arr_len);
            if (do_senddiag(sg_fd, 1, data_arr, arr_len + 4, 1, verbose)) {
                fprintf(stderr, "couldn't send Array control page\n");
                goto err_out;
            }
            break;
        case 0xc:       /* Subenclosure String Out diagnostic page */
            printf("Sending Subenclosure String Out [0x%x] page, with page "
                   "length=%d bytes\n", page_code, arr_len);
            if (do_senddiag(sg_fd, 1, data_arr, arr_len + 4, 1, verbose)) {
                fprintf(stderr, "couldn't send Subenclosure String Out "
                        "page\n");
                goto err_out;
            }
            break;
        default:
            fprintf(stderr, "Setting SES control page 0x%x not supported "
                    "yet\n", page_code);
            break;
        }
    }

    ret = 0;
err_out:
    res = close(sg_fd);
    if (res < 0) {
        perror(ME "close error");
        return 2;
    }
    return ret;
}