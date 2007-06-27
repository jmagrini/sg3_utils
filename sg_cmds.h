#ifndef SG_CMDS_H
#define SG_CMDS_H


extern const char * sg_cmds_version();

extern int sg_ll_inquiry(int sg_fd, int cmddt, int evpd, int pg_op,
                         void * resp, int mx_resp_len, int noisy,
                         int verbose);

extern int sg_ll_test_unit_ready(int sg_fd, int pack_id, int noisy,
                                 int verbose);

extern int sg_ll_sync_cache(int sg_fd, int sync_nv, int immed, int noisy,
                            int verbose);

extern int sg_ll_readcap_10(int sg_fd, int pmi, unsigned int lba,
                            void * resp, int mx_resp_len, int verbose);

extern int sg_ll_readcap_16(int sg_fd, int pmi, unsigned long long llba,
                            void * resp, int mx_resp_len, int verbose);

extern int sg_ll_mode_sense6(int sg_fd, int dbd, int pc, int pg_code,
                             int sub_pg_code, void * resp, int mx_resp_len,
                             int noisy, int verbose);

extern int sg_ll_mode_sense10(int sg_fd, int dbd, int pc, int pg_code,
                              int sub_pg_code, void * resp, int mx_resp_len,
                              int noisy, int verbose);

extern int sg_ll_mode_select6(int sg_fd, int pf, int sp, void * paramp,
                              int param_len, int noisy, int verbose);

extern int sg_ll_mode_select10(int sg_fd, int pf, int sp, void * paramp,
                               int param_len, int noisy, int verbose);

extern int sg_ll_request_sense(int sg_fd, int desc, void * resp,
                               int mx_resp_len, int verbose);

extern int sg_ll_report_luns(int sg_fd, int select_report, void * resp,
                             int mx_resp_len, int noisy, int verbose);

extern int sg_ll_log_sense(int sg_fd, int ppc, int sp, int pc, int pg_code,
                           int paramp, unsigned char * resp, int mx_resp_len,
                           int noisy, int verbose);

extern int sg_ll_log_select(int sg_fd, int pcr, int sp, int pc,
                            unsigned char * paramp, int param_len,
                            int noisy, int verbose);

struct sg_simple_inquiry_resp {
    unsigned char peripheral_qualifier;
    unsigned char peripheral_type;
    unsigned char rmb;
    unsigned char version;      /* as per recent drafts: whole of byte 2 */
    unsigned char byte_3;
    unsigned char byte_5;
    unsigned char byte_6;
    unsigned char byte_7;
    char vendor[9];
    char product[17];
    char revision[5];
};

extern int sg_simple_inquiry(int sg_fd,
                             struct sg_simple_inquiry_resp * inq_data,
                             int noisy, int verbose);

extern int sg_mode_page_offset(const unsigned char * resp, int resp_len,
                               int mode_sense_6, char * err_buff,
                               int err_buff_len);
#endif