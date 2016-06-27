/**
 * Post decoding stage that display the decoded video in a GTK display
 * using Cairo
 */

#ifndef _DISPLAY_STAGE_H_
#define _DISPLAY_STAGE_H_ (1)

#include <ardrone_tool/Video/video_stage.h>
#include <inttypes.h>
#include <gtk/gtk.h>

typedef struct _display_stage_cfg_ {
    // PARAM
    float bpp;
    vp_api_picture_t *decoder_info;

    // INTERNAL
    uint8_t *frameBuffer;
    uint32_t fbSize;
    bool_t paramsOK;

    GtkWidget *widget;
} display_stage_cfg_t;

// my struct
struct tran_data {
    int size;
    int width;
    int height;
    int frame_id;
};

struct area_err {
    float x_err;
    float y_err;
    float z_err;
};

static struct tran_data *shared_info;
struct area_err *err_info;
static uint8_t *shared_data;
static int info_shmid, data_shmid, err_shmid;
static void *info_shm, *data_shm, *err_shm;
static int sem_id, sem_id2, sem_id3;
static int pre_err_id;

static const key_t SHARE_KEY = 1333;
static const key_t DATA_KEY = 1313;
static const key_t SEM_KEY = 9999;
static const key_t SEM_KEY2 = 7777;
static const key_t SEM_KEY3 = 6666;
static const key_t ERR_KEY = 1995;
static const int DATA_SIZE = 1048576;

C_RESULT display_stage_open (display_stage_cfg_t *cfg);
C_RESULT display_stage_transform (display_stage_cfg_t *cfg, vp_api_io_data_t *in, vp_api_io_data_t *out);
C_RESULT display_stage_close (display_stage_cfg_t *cfg);

extern const vp_api_stage_funcs_t display_stage_funcs;

#endif //_DISPLAY_STAGE_H_
