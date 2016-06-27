/**
 * @file ardrone_testing_tool.c
 * @author nicolas.brulez@parrot.com
 * @date 2012/09/04
 *
 * Example of AR.Drone Live Video Feed using the AR.Drone SDK 2.0
 * This example works for both AR.Drone 1 and 2
 *
 * Show example of pre-decoding processing (record encoded h.264 frames)
 * and post-decoding processing (display decoded frames)
 *
 * Possible args :
 *  -eFileName : Record encoded video to FileName
 *               - If AR.Drone 2.0 is used, and filename ends with ".h264"
 *               it can be read with any standard video player that supports
 *               raw h264 (ffplay, mplayer ...)
 *               - AR.Drone 1 video must be transcoded before use. See iOS
 *               AR.FreeFlight app for an example
 *         NOTE : if -eNAME arg is not present, encoded video will not be recorded
 *         NOTE : This is NOT the equivalent of the official record function for AR.Drone2 !
 *
 *  -b : use bottom camera instead of frontal camera
 *
 *  -c : use alternative video codec
 *       - For AR.Drone 2 -> 720p instead of 360p (both h.264)
 *       - For AR.Drone 1 -> VLIB instead of P264
 *
 * NOTE : Frames will be displayed only if out_picture->format is set to PIX_FMT_RGB565
 *
 * Display examlpe uses GTK2 + Cairo.
 */

// Generic includes
#include <ardrone_api.h>
#include <signal.h>

// ARDrone Tool includes
#include <ardrone_tool/ardrone_tool.h>
#include <ardrone_tool/ardrone_tool_configuration.h>
#include <ardrone_tool/ardrone_version.h>
#include <ardrone_tool/Video/video_stage.h>
#include <ardrone_tool/Video/video_recorder_pipeline.h>
#include <ardrone_tool/Navdata/ardrone_navdata_client.h>

// App includes
#include <Video/pre_stage.h>
#include <Video/display_stage.h>

// GTK includes
#include <gtk/gtk.h>

 //Custom include
 #include <curses.h>
 #include <sys/timeb.h>

int exit_program = 1;

pre_stage_cfg_t precfg;
display_stage_cfg_t dispCfg;

codec_type_t drone1Codec = P264_CODEC;
codec_type_t drone2Codec = H264_360P_CODEC;
ZAP_VIDEO_CHANNEL videoChannel = ZAP_CHANNEL_HORI;

#define FILENAMESIZE (256)
char encodedFileName[FILENAMESIZE] = {0};

void controlCHandler (int signal)
{
    // Flush all streams before terminating
    // delete shared memory and semaphore
    del_share_memory(info_shm, info_shmid);
    del_share_memory(data_shm, data_shmid);
    del_share_memory(err_shm, err_shmid);
    del_semaphore(sem_id);
    // Flush all streams before terminating
    fflush (NULL);
    usleep (200000); // Wait 200 msec to be sure that flush occured
    printf ("\nAll files were flushed\n");
    endwin();
    exit (0);
}

/**
 * This example shows how to get the AR.Drone Live Video feed
 */
int main (int argc, char *argv[])
{
    signal (SIGABRT, &controlCHandler);
    signal (SIGTERM, &controlCHandler);
    signal (SIGINT, &controlCHandler);
    int prevargc = argc;
    char **prevargv = argv;

    int index = 0;
    for (index = 1; index < argc; index++)
    {
        if ('-' == argv[index][0] &&
            'e' == argv[index][1])
        {
            char *fullname = argv[index];
            char *name = &fullname[2];
            strncpy (encodedFileName, name, FILENAMESIZE);
        }

        if ('-' == argv[index][0] &&
            'c' == argv[index][1])
        {
            drone1Codec = UVLC_CODEC;
            drone2Codec = H264_720P_CODEC;
        }

        if ('-' == argv[index][0] &&
            'b' == argv[index][1])
        {
            videoChannel = ZAP_CHANNEL_VERT;
        }
    }

    gtk_init (&prevargc, &prevargv);

    return ardrone_tool_main (prevargc, prevargv);
}

C_RESULT ardrone_tool_init_custom (void)
{

    /**
     * Set application default configuration
     *
     * In this example, we use the AR.FreeFlight configuration :
     * - Demo navdata rate (15Hz)
     * - Useful additionnal navdata packets enabled (detection, games, video record, wifi quality estimation ...)
     * - Adaptive video enabled (bitrate_ctrl_mode) -> video bitrate will change according to the available bandwidth
     */
    ardrone_application_default_config.navdata_demo = TRUE;
    ardrone_application_default_config.navdata_options = (NAVDATA_OPTION_MASK(NAVDATA_DEMO_TAG) | NAVDATA_OPTION_MASK(NAVDATA_VISION_DETECT_TAG) | NAVDATA_OPTION_MASK(NAVDATA_GAMES_TAG) | NAVDATA_OPTION_MASK(NAVDATA_MAGNETO_TAG) | NAVDATA_OPTION_MASK(NAVDATA_HDVIDEO_STREAM_TAG) | NAVDATA_OPTION_MASK(NAVDATA_WIFI_TAG));
    if (IS_ARDRONE2)
    {
        ardrone_application_default_config.video_codec = drone2Codec;
    }
    else
    {
        ardrone_application_default_config.video_codec = drone1Codec;
    }
    ardrone_application_default_config.video_channel = videoChannel;
    ardrone_application_default_config.bitrate_ctrl_mode = 1;

    /**
     * Define the number of video stages we'll add before/after decoding
     */
#define EXAMPLE_PRE_STAGES 1
#define EXAMPLE_POST_STAGES 1

    /**
     * Allocate useful structures :
     * - index counter
     * - thread param structure and its substructures
     */
    uint8_t stages_index = 0;

    specific_parameters_t *params = (specific_parameters_t *)vp_os_calloc (1, sizeof (specific_parameters_t));
    specific_stages_t *example_pre_stages = (specific_stages_t *)vp_os_calloc (1, sizeof (specific_stages_t));
    specific_stages_t *example_post_stages = (specific_stages_t *)vp_os_calloc (1, sizeof (specific_stages_t));
    vp_api_picture_t *in_picture = (vp_api_picture_t *)vp_os_calloc (1, sizeof (vp_api_picture_t));
    vp_api_picture_t *out_picture = (vp_api_picture_t *)vp_os_calloc (1, sizeof (vp_api_picture_t));

    /**
     * Fill the vp_api_pictures used for video decodig
     * --> out_picture->format is mandatory for AR.Drone 1 and 2. Other lines are only necessary for AR.Drone 1 video decoding
     */
    in_picture->width = 640; // Drone 1 only : Must be greater than the drone 1 picture size (320)
    in_picture->height = 360; // Drone 1 only : Must be greater that the drone 1 picture size (240)

    out_picture->framerate = 20; // Drone 1 only, must be equal to drone target FPS
    out_picture->format = PIX_FMT_RGB565; // MANDATORY ! Only RGB24, RGB565 are supported
    out_picture->width = in_picture->width;
    out_picture->height = in_picture->height;

    // Alloc Y, CB, CR bufs according to target format
    uint32_t bpp = 0;
    switch (out_picture->format)
    {
    case PIX_FMT_RGB24:
        // One buffer, three bytes per pixel
        bpp = 3;
        out_picture->y_buf = vp_os_malloc ( out_picture->width * out_picture->height * bpp );
        out_picture->cr_buf = NULL;
        out_picture->cb_buf = NULL;
        out_picture->y_line_size = out_picture->width * bpp;
        out_picture->cb_line_size = 0;
        out_picture->cr_line_size = 0;
        break;
    case PIX_FMT_RGB565:
        // One buffer, two bytes per pixel
        bpp = 2;
        out_picture->y_buf = vp_os_malloc ( out_picture->width * out_picture->height * bpp );
        out_picture->cr_buf = NULL;
        out_picture->cb_buf = NULL;
        out_picture->y_line_size = out_picture->width * bpp;
        out_picture->cb_line_size = 0;
        out_picture->cr_line_size = 0;
        break;
    default:
        fprintf (stderr, "Wrong video format, must be either PIX_FMT_RGB565 or PIX_FMT_RGB24\n");
        exit (-1);
        break;
    }

    /**
     * Allocate the stage lists
     *
     * - "pre" stages are called before video decoding is done
     *  -> A pre stage get the encoded video frame (including PaVE header for AR.Drone 2 frames) as input
     *  -> A pre stage MUST NOT modify these data, and MUST pass it to the next stage
     * - Typical "pre" stage : Encoded video recording for AR.Drone 1 (recording for AR.Drone 2 is handled differently)
     *
     * - "post" stages are called after video decoding
     *  -> The first post stage will get the decoded video frame as its input
     *   --> Video frame format depend on out_picture->format value (RGB24 / RGB565)
     *  -> A post stage CAN modify the data, as ardrone_tool won't process it afterwards
     *  -> All following post stages will use the output of the previous stage as their inputs
     * - Typical "post" stage : Display the decoded frame
     */
    example_pre_stages->stages_list = (vp_api_io_stage_t *)vp_os_calloc (EXAMPLE_PRE_STAGES, sizeof (vp_api_io_stage_t));
    example_post_stages->stages_list = (vp_api_io_stage_t *)vp_os_calloc (EXAMPLE_POST_STAGES, sizeof (vp_api_io_stage_t));

    /**
     * Fill the PRE stage list
     * - name and type are debug infos only
     * - cfg is the pointer passed as "cfg" in all the stages calls
     * - funcs is the pointer to the stage functions
     */
    stages_index = 0;

    vp_os_memset (&precfg, 0, sizeof (pre_stage_cfg_t));
    strncpy (precfg.outputName, encodedFileName, 255);

    example_pre_stages->stages_list[stages_index].name = "Encoded Dumper"; // Debug info
    example_pre_stages->stages_list[stages_index].type = VP_API_FILTER_DECODER; // Debug info
    example_pre_stages->stages_list[stages_index].cfg  = &precfg;
    example_pre_stages->stages_list[stages_index++].funcs  = pre_stage_funcs;

    example_pre_stages->length = stages_index;

    /**
     * Fill the POST stage list
     * - name and type are debug infos only
     * - cfg is the pointer passed as "cfg" in all the stages calls
     * - funcs is the pointer to the stage functions
     */
    stages_index = 0;

    vp_os_memset (&dispCfg, 0, sizeof (display_stage_cfg_t));
    dispCfg.bpp = bpp;
    dispCfg.decoder_info = in_picture;

    example_post_stages->stages_list[stages_index].name = "Decoded display"; // Debug info
    example_post_stages->stages_list[stages_index].type = VP_API_OUTPUT_SDL; // Debug info
    example_post_stages->stages_list[stages_index].cfg  = &dispCfg;
    example_post_stages->stages_list[stages_index++].funcs  = display_stage_funcs;

    example_post_stages->length = stages_index;

    /**
     * Fill thread params for the ardrone_tool video thread
     *  - in_pic / out_pic are reference to our in_picture / out_picture
     *  - pre/post stages lists are references to our stages lists
     *  - needSetPriority and priority are used to control the video thread priority
     *   -> if needSetPriority is set to 1, the thread will try to set its priority to "priority"
     *   -> if needSetPriority is set to 0, the thread will keep its default priority (best on PC)
     */
    params->in_pic = in_picture;
    params->out_pic = out_picture;
    params->pre_processing_stages_list  = example_pre_stages;
    params->post_processing_stages_list = example_post_stages;
    params->needSetPriority = 0;
    params->priority = 0;

    /**
     * Start the video thread (and the video recorder thread for AR.Drone 2)
     */
    START_THREAD(video_stage, params);
    video_stage_init();
    if (2 <= ARDRONE_VERSION ())
    {
        START_THREAD (video_recorder, NULL);
        video_recorder_init ();
    }

    video_stage_resume_thread ();

   //START_THREAD(keyboard_control, 0); //write by custom
   START_THREAD(auto_control, 0);
   START_THREAD(control_switch, 0);
    return C_OK;
}

C_RESULT ardrone_tool_shutdown_custom ()
{
    video_stage_resume_thread(); //Resume thread to kill it !
    JOIN_THREAD(video_stage);
    if (2 <= ARDRONE_VERSION ())
    {
        video_recorder_resume_thread ();
        JOIN_THREAD (video_recorder);
    }

    //JOIN_THREAD(keyboard_control); //write  by custom
    JOIN_THREAD(auto_control);
    JOIN_THREAD(control_switch);
    return C_OK;
}

bool_t ardrone_tool_exit ()
{
    return exit_program == 0;
}

/**
 * Declare Thread / Navdata tables
 */
 long long getSystemTime() {
    struct timeb t;
    ftime(&t);
    return 1000* t.time + t.millitm;
 }

 void free_flight(int flags, float phi, float theta, float gaz, float yaw) {
    long long start = getSystemTime();
    long long end = getSystemTime();
    while (end - start < 10) {
        ardrone_at_set_progress_cmd(flags, phi, theta, gaz, yaw);
        end = getSystemTime();
    }
 }

const float alpha = 0.3;
const float beta = 0.7;
static volatile int tracking = 0;

PROTO_THREAD_ROUTINE(keyboard_control, NO_PARAM); //
PROTO_THREAD_ROUTINE(auto_control, NO_PARAM);
PROTO_THREAD_ROUTINE(control_switch, NO_PARAM);

//  Control funcs
void takeOff(GtkWidget *widget, gpointer *data)
{
     ardrone_tool_set_ui_pad_start(1);
     printf("takeOff\n");
}

void land(GtkWidget *widget, gpointer *data)
{
    int t = 0;
    tracking = 0;
    ardrone_tool_set_ui_pad_start(0);
    printf("land\n");
}

void tracking_func(GtkWidget *widget, gpointer *data)
{
    tracking = 1;
    printf("tracking\n");
}

void stopTracking(GtkWidget *widget, gpointer *data)
{
    tracking = 0;
    printf("stop_tracking\n");
}

//这是一个回调函数，用来响应关闭信号
void destroy(GtkWidget *widget, gpointer *data)
{
    gtk_main_quit();
}

char* button_names[4] = {
    "takeOfff",
    "land",
    "tracking",
    "stop",
};

void func()  {}

void (*tracking_funcs[4])(void) = {takeOff, land, tracking_func, stopTracking};

DEFINE_THREAD_ROUTINE(control_switch, NO_PARAM) {
    int k, button_n = 4;
 //定义指向控件的指针
    GtkWidget *window;
    GtkWidget *buttons[4];
    GtkWidget *box;

    //初始化图形显示环境
    gtk_init(0, NULL);

    //创建窗口，并设置当关闭窗口时要执行的回调函数
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
     gtk_window_set_title(GTK_WINDOW(window),"ardrone controler");
    g_signal_connect(GTK_OBJECT(window), "destroy",
        GTK_SIGNAL_FUNC(destroy), NULL);
/*添加一个box到window*/
    box=gtk_hbox_new(FALSE,0);
    gtk_container_add(GTK_CONTAINER(window),box);

    //设置窗口属性
    gtk_container_border_width(GTK_CONTAINER(window), 20);

    for (k = 0; k < button_n; k++) {
        //创建按钮，并设置当单击按钮时要执行的回调函数
        buttons[k] = gtk_button_new_with_label(button_names[k]);
        g_signal_connect (buttons[k], "clicked", G_CALLBACK (tracking_funcs[k]), "data");
        /*向box放入button*/
        gtk_box_pack_start(GTK_BOX(box),buttons[k],FALSE,FALSE,3);
        gtk_widget_show(buttons[k]);/*告诉GTK此按钮完整，并且可以显示*/
    }
/*    GtkWidget *button1;
    button1=gtk_button_new_with_label("takeOff");
    g_signal_connect (button1, "clicked", G_CALLBACK (takeOff), "data");
    gtk_box_pack_start(GTK_BOX(box),button1,FALSE,FALSE,3);
    gtk_widget_show(button1);

    GtkWidget *button2;
    button2=gtk_button_new_with_label("land");
    g_signal_connect (button2, "clicked", G_CALLBACK (land), "data");
    gtk_box_pack_start(GTK_BOX(box),button2,FALSE,FALSE,3);
    gtk_widget_show(button2);*/

    //显示窗体和按钮
    gtk_widget_show(box);
    gtk_widget_show(window);

    //进入消息处理循环
    gtk_main();
    return (THREAD_RET)0;
}

DEFINE_THREAD_ROUTINE(auto_control, NO_PARAM) {
    while (true) {
        if (!tracking) {
            continue;
        }
        if (!semaphore_P(sem_id3))
            exit(EXIT_FAILURE);
        float x_err = err_info->x_err;
        float y_err = -err_info->y_err;
        float z_err = -err_info->z_err;
        if (!semaphore_V(sem_id3))
            exit(EXIT_FAILURE);
        if (x_err > 0.18) free_flight(3,0,0,0,x_err);
        else if (z_err > 0.25) free_flight(3, 0, z_err, 0, 0);
        else {
            free_flight(3,0,0,0,x_err);
            free_flight(3,0,0,y_err,0);
            free_flight(3, 0, z_err, 0, 0);
        }
        //free_flight(3, 0, z_err, y_err, x_err);
    }
    return (THREAD_RET)0;
}

DEFINE_THREAD_ROUTINE(keyboard_control, NO_PARAM) { //
    int take_off_bit = 0;
    /*sleep(2);*/
    initscr();
    /*nonl();*/
    noecho();
    printw("ready to fly");
    int control = 0;
    while (control != 'q') {
        control = getch();
        /*if (control == 't') {
            printw("press t");
            tracking = 1;
        }
        if (control == 'b') {
            tracking = 0;
        }*/
        if (take_off_bit == 0) {
            if (control == 'e') {
                 printw("take off");
                 ardrone_tool_set_ui_pad_start(1);
                 take_off_bit = 1;
            }
        } else {
            //free_flight(0, 0, 0, 0, 0);
            switch(control) {
                case 'r':
                    printw("land");
                    int t = 0;
                    tracking = 0;
                    for (; t < 100; t++) ardrone_tool_set_ui_pad_start(0);
                    take_off_bit = 0;
                    break;
                case 'w':
                    printw("forward");
                    free_flight(3, 0, -alpha, 0, 0);        
                    break;
                case 's':
                    printw("back");
                    free_flight(3, 0, alpha, 0, 0);
                    break;
                case 'a':
                    printw("left");
                    free_flight(3, -alpha, 0, 0, 0);
                    break;
                case 'd':
                    printw("right");
                    free_flight(3, alpha, 0, 0, 0);
                    break;
                case 'h':
                    printw("hovering");
                     free_flight(0, 0, 0, 0, 0);
                    break;
                case 'u':
                    printw("turn left");
                    free_flight(3, 0, 0, 0, -alpha);
                    break;
                case 'o': 
                    printw("turn right");
                    free_flight(3, 0, 0, 0, alpha);
                    break;
                case 'i':
                    printw("up");
                    free_flight(3, 0, 0, alpha, 0);
                    break;
                case 'k':
                    printw("down");
                     free_flight(3, 0, 0, -alpha, 0);
                    break;
                case 't':
                    printw("tracking");
                    tracking = 1;
                    break;
                case 'y':
                    printw("stop tracking");
                    tracking = 0;
                    break;
                default:
                    break;
            }
        }
        clear();
    }
    endwin();
    return (THREAD_RET)0;
}

// Declare gtk thread and include it in the thread table
//  This is needed because the display_stage.c file can't access this table
PROTO_THREAD_ROUTINE(gtk, data);

BEGIN_THREAD_TABLE
THREAD_TABLE_ENTRY(video_stage, 20)
THREAD_TABLE_ENTRY(video_recorder, 20)
THREAD_TABLE_ENTRY(navdata_update, 20)
THREAD_TABLE_ENTRY(ardrone_control, 20)
THREAD_TABLE_ENTRY(keyboard_control, 20)
THREAD_TABLE_ENTRY(auto_control, 20)
THREAD_TABLE_ENTRY(control_switch, 20)
THREAD_TABLE_ENTRY(gtk, 20)
END_THREAD_TABLE

BEGIN_NAVDATA_HANDLER_TABLE
END_NAVDATA_HANDLER_TABLE