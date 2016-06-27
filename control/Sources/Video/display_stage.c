/**
 * @file display_stage.c
 * @author nicolas.brulez@parrot.com
 * @date 2012/09/25
 *
 * This stage is a naive example of how to display video using GTK2 + Cairo
 * In a complete application, all GTK handling (gtk main thread + widgets/window creation)
 *  should NOT be handled by the video pipeline (see the Navigation linux example)
 *
 * The window will be resized according to the picture size, and should not be resized bu the user
 *  as we do not handle any gtk event except the expose-event
 *
 * This example is not intended to be a GTK/Cairo tutorial, it is only an example of how to display
 *  the AR.Drone live video feed. The GTK Thread is started here to improve the example readability
 *  (we have all the gtk-related code in one file)
 */
//my header file
#include <unistd.h>  
#include <stdlib.h>  
#include <fcntl.h>  
#include <limits.h>  
#include <sys/types.h>  
#include <sys/stat.h>  
#include <stdio.h>  
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include "semaphore.h"

// Self header file
#include "display_stage.h"

// GTK/Cairo headers
#include <cairo.h>
#include <gtk/gtk.h>
/*// my global
static IplImage *currframe = NULL;
static IplImage *dst = NULL;*/


// my func
void* create_shared_memory(key_t key, size_t size, int *shmid) {
    void *shm = NULL;

    *shmid = shmget((key_t)key, size, 0666|IPC_CREAT);
    if (*shmid == -1) {
        fprintf(stderr, "shmget failed\n");
        exit(EXIT_FAILURE);
    }

    shm = shmat(*shmid, 0, 0);
    if (shm == (void*)-1) {
        fprintf(stderr, "shmat failed\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "create shmid:%d\n", *shmid);
    return shm;
}

// end my func

// Funcs pointer definition
const vp_api_stage_funcs_t display_stage_funcs = {
    NULL,
    (vp_api_stage_open_t) display_stage_open,
    (vp_api_stage_transform_t) display_stage_transform,
    (vp_api_stage_close_t) display_stage_close
};

// Extern so we can make the ardrone_tool_exit() function (ardrone_testing_tool.c)
// return TRUE when we close the video window
extern int exit_program;

// Boolean to avoid asking redraw of a not yet created / destroyed window
bool_t gtkRunning = FALSE;

// Picture size getter from input buffer size
// This function only works for RGB565 buffers (i.e. 2 bytes per pixel)
static void getPicSizeFromBufferSize (uint32_t bufSize, uint32_t *width, uint32_t *height)
{
    if (NULL == width || NULL == height)
    {
        return;
    }

    switch (bufSize)
    {
    case 50688: //QCIF > 176*144 *2bpp
        *width = 176;
        *height = 144;
        break;
    case 153600: //QVGA > 320*240 *2bpp
        *width = 320;
        *height = 240;
        break;
    case 460800: //360p > 640*360 *2bpp
        *width = 640;
        *height = 360;
        break;
    case 1843200: //720p > 1280*720 *2bpp
        *width = 1280;
        *height = 720;
        break;
    default:
        *width = 0;
        *height = 0;
        break;
    }
}

// Get actual frame size (without padding)
void getActualFrameSize (display_stage_cfg_t *cfg, uint32_t *width, uint32_t *height)
{
    if (NULL == cfg || NULL == width || NULL == height)
    {
        return;
    }

    *width = cfg->decoder_info->width;
    *height = cfg->decoder_info->height;
}

// Redraw function, called by GTK each time we ask for a frame redraw
static gboolean
on_expose_event (GtkWidget *widget,
                 GdkEventExpose *event,
                 gpointer data)
{
    display_stage_cfg_t *cfg = (display_stage_cfg_t *)data;

    if (2.0 != cfg->bpp)
    {
        return FALSE;
    }

    uint32_t width = 0, height = 0, stride = 0;
    getPicSizeFromBufferSize (cfg->fbSize, &width, &height);
    stride = cfg->bpp * width;

    if (0 == stride)
    {
        return FALSE;
    }

    uint32_t actual_width = 0, actual_height = 0;
    getActualFrameSize (cfg, &actual_width, &actual_height);
    gtk_window_resize (GTK_WINDOW (widget), actual_width, actual_height);

    cairo_t *cr = gdk_cairo_create (widget->window);

    cairo_surface_t *surface = cairo_image_surface_create_for_data (cfg->frameBuffer, CAIRO_FORMAT_RGB16_565, width, height, stride);

    // my code
    if (!semaphore_P(sem_id))
        exit(EXIT_FAILURE);
        shared_info->width = actual_width;
        shared_info->height = actual_height;
        shared_info->size = cfg->fbSize;
        shared_info->frame_id += 1;
        vp_os_memcpy(shared_data, cfg->frameBuffer, cfg->fbSize);
    if (!semaphore_V(sem_id))
        exit(EXIT_FAILURE);

    cairo_set_source_surface (cr, surface, 0.0, 0.0);

    cairo_paint (cr);

    cairo_surface_destroy (surface);

    cairo_destroy (cr);

    return FALSE;
}

/**
 * Main GTK Thread.
 * On an actual application, this thread should be started from your app main thread, and not from a video stage
 * This thread will handle all GTK-related functions
 */
DEFINE_THREAD_ROUTINE(gtk, data)
{
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

    display_stage_cfg_t *cfg = (display_stage_cfg_t *)data;
    cfg->widget = window;

    g_signal_connect (window, "expose-event", G_CALLBACK (on_expose_event), data);
    g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

    gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size (GTK_WINDOW (window), 10, 10);
    gtk_widget_set_app_paintable (window, TRUE);
    gtk_widget_set_double_buffered (window, FALSE);

    gtk_widget_show_all (window);

    gtkRunning = TRUE;

    gtk_main ();

    gtkRunning = FALSE;

    // Force ardrone_tool to close
    exit_program = 0;

    // Sometimes, ardrone_tool might not finish properly
    // This happens mainly because a thread is blocked on a syscall
    // in this case, wait 5 seconds then kill the app
    sleep (5);
    exit (0);

    return (THREAD_RET)0;
}

C_RESULT display_stage_open (display_stage_cfg_t *cfg)
{
    // Check that we use RGB565
    if (2 != cfg->bpp)
    {
        // If that's not the case, then don't display anything
        cfg->paramsOK = FALSE;
    }
    else
    {
        // Else, start GTK thread and window
        cfg->paramsOK = TRUE;
        cfg->frameBuffer = NULL;
        cfg->fbSize = 0;
        START_THREAD (gtk, cfg);
        
        // shared memory
        info_shm = create_shared_memory(SHARE_KEY, sizeof(struct tran_data), &info_shmid);
        data_shm = create_shared_memory(DATA_KEY, sizeof(uint8_t)*DATA_SIZE, &data_shmid);
        err_shm = create_shared_memory(ERR_KEY, sizeof(struct area_err), &err_shmid);
        shared_info = (struct tran_data*) info_shm;
        shared_data = (uint8_t*) data_shm;
        err_info = (struct area_err*) err_shm;

        shared_info->frame_id = -1;

        // semaphore
        sem_id = semget(SEM_KEY, 1, 0666 | IPC_CREAT);
        sem_id2 = semget(SEM_KEY2, 1, 0666 | IPC_CREAT);
        sem_id3 = semget(SEM_KEY3, 1, 0666 | IPC_CREAT);
        if (!set_semvalue(sem_id)) {
                fprintf(stderr, "Failed to init semaphore\n");
                exit(EXIT_FAILURE);
        }
        if (!set_semvalue(sem_id3)) {
                fprintf(stderr, "Failed to init semaphore\n");
                exit(EXIT_FAILURE);
        }
        err_info->x_err = 0;
        err_info->y_err = 0;
        err_info->z_err = 0;
        //
        //namedWindow( "CamShift Demo", 0 );
    }
    return C_OK;
}

C_RESULT display_stage_transform (display_stage_cfg_t *cfg, vp_api_io_data_t *in, vp_api_io_data_t *out)
{
    // Process only if we are using RGB565
    if (FALSE == cfg->paramsOK)
    {
        return C_OK;
    }
    // Realloc frameBuffer if needed
    if (in->size != cfg->fbSize)
    {
        cfg->frameBuffer = (uint8_t*)vp_os_realloc (cfg->frameBuffer, in->size);
        cfg->fbSize = in->size;
    }
    // Copy last frame to frameBuffer
    vp_os_memcpy (cfg->frameBuffer, in->buffers[in->indexBuffer], cfg->fbSize);

    // Ask GTK to redraw the window
    uint32_t width = 0, height = 0;
    getPicSizeFromBufferSize (in->size, &width, &height);
    if (TRUE == gtkRunning)
    {
        gtk_widget_queue_draw_area (cfg->widget, 0, 0, width, height);
    }

    // Tell the pipeline that we don't have any output
    out->size = 0;

    // my test
/*    if (NULL != currframe) {
        cv::Mat image = cv::cvarrToMat(currframe);
        imshow( "CamShift Demo", image );
    }*/

    return C_OK;
}

void del_share_memory(void* shm, int shmid) {
    if (shmdt(shm) == -1) {
        fprintf(stderr, "shmdt failed\n");
        exit(EXIT_FAILURE);
    }

    if (shmctl(shmid, IPC_RMID, 0) == -1) {
        fprintf(stderr, "shmtcl(RMID) failed\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "del shm_id:%d\n", shmid);
}

C_RESULT display_stage_close (display_stage_cfg_t *cfg)
{
    // Free all allocated memory
    if (NULL != cfg->frameBuffer)
    {
        vp_os_free (cfg->frameBuffer);
        cfg->frameBuffer = NULL;
    }

    // delete shared memory and semaphore
    del_share_memory(info_shm, info_shmid);
    del_share_memory(data_shm, data_shmid);
    del_share_memory(err_shm, err_shmid);
    del_semaphore(sem_id);
    return C_OK;
}
