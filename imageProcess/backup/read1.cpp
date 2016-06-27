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
#include <pthread.h>
#include "semaphore.h"

#include <opencv2/opencv.hpp>
using namespace cv;
using namespace std;

#include <iostream>
#include <ctype.h>

#define RGB565_MASK_RED        0xF800
#define RGB565_MASK_GREEN                         0x07E0
#define RGB565_MASK_BLUE                         0x001F
#define UpAlign4(v)     (((v) + 0x3) & 0xFFFFFFFC)

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
static int ini_area = 150 * 150;
static struct tran_data* shared_info;
static struct area_err *err_info;
static uint8_t *shared_data;
static uint8_t *data = NULL;
static uint8_t *realdata = NULL;
static pthread_t ntid;
static int sem_id;
static int sem_id2;
static int height;
static int width;

int pre_frame_id = -1;

static const key_t SHARE_KEY = 1333;
static const key_t DATA_KEY = 1313;
static const key_t SEM_KEY = 9999;
static const key_t ERR_KEY = 1995;
static const key_t SEM_KEY2 = 7777;
static const int DATA_SIZE = 1048576;

// camshift global
Mat image;

bool backprojMode = false;
bool selectObject = false;
int trackObject = 0;
bool showHist = true;
Point origin;
Rect selection;
int vmin = 10, vmax = 256, smin = 30;

static void onMouse( int event, int x, int y, int, void* )
{
    if( selectObject )
    {
        selection.x = MIN(x, origin.x);
        selection.y = MIN(y, origin.y);
        selection.width = std::abs(x - origin.x);
        selection.height = std::abs(y - origin.y);

        selection &= Rect(0, 0, image.cols, image.rows);
    }

    switch( event )
    {
    case EVENT_LBUTTONDOWN:
        origin = Point(x,y);
        selection = Rect(x,y,0,0);
        selectObject = true;
        break;
    case EVENT_LBUTTONUP:
        selectObject = false;
        if( selection.width > 0 && selection.height > 0 ) {
            trackObject = -1;
        }
        break;
    }
}

string hot_keys =
    "\n\nHot keys: \n"
    "\tESC - quit the program\n"
    "\tc - stop the tracking\n"
    "\tb - switch to/from backprojection view\n"
    "\th - show/hide object histogram\n"
    "\tp - pause video\n"
    "To initialize tracking, select the object with mouse\n";

const char* keys =
{
    "{help h | | show help message}{@camera_number| 0 | camera number}"
};
// camshift global

void* create_shared_memory(key_t key, size_t size, int& shmid) {
    void *shm = NULL;

    shmid = shmget((key_t)key, size, 0666|IPC_CREAT);
    if (shmid == -1) {
        fprintf(stderr, "shmget failed\n");
        exit(EXIT_FAILURE);
    }

    shm = shmat(shmid, 0, 0);
    if (shm == (void*)-1) {
        fprintf(stderr, "shmat failed\n");
        exit(EXIT_FAILURE);
    }
    return shm;
}

static void rgb565_to_rgb888(const void * psrc, int w, int h, void * pdst)    
{    
    int srclinesize = UpAlign4(w * 2);    
    int dstlinesize = UpAlign4(w * 3);    
    
    const unsigned char  * psrcline;    
    const unsigned short * psrcdot;    
    unsigned char  * pdstline;    
    unsigned char  * pdstdot;    
    
    int i,j;    
    
    if (!psrc || !pdst || w <= 0 || h <= 0) {    
        printf("rgb565_to_rgb888 : parameter error\n");    
        return;    
    }    
    
    psrcline = (const unsigned char *)psrc;    
    pdstline = (unsigned char *)pdst;    
    for (i=0; i<h; i++) {    
        psrcdot = (const unsigned short *)psrcline;    
        pdstdot = pdstline;    
        for (j=0; j<w; j++) {    
            //565 b|g|r -> 888 r|g|b    
            *pdstdot++ = (unsigned char)(((*psrcdot) >> 0 ) << 3);    
            *pdstdot++ = (unsigned char)(((*psrcdot) >> 5 ) << 2);    
            *pdstdot++ = (unsigned char)(((*psrcdot) >> 11) << 3);    
            psrcdot++;    
        }    
        psrcline += srclinesize;    
        pdstline += dstlinesize;    
    } 
}

void *copy_image_thread(void *arg) {
    printf("waiting frame\n");
    for(;;) {
        if (!semaphore_P(sem_id))
            exit(EXIT_FAILURE);
        if (!semaphore_P(sem_id2))
            exit(EXIT_FAILURE);
        if (shared_info->frame_id != pre_frame_id) {
            if (NULL == data)
                data = (uint8_t*) malloc(sizeof(uint8_t)*shared_info->size);

            width = shared_info->width;
            height = shared_info->height;

            memcpy(data, shared_data, shared_info->size);

            pre_frame_id = shared_info->frame_id;
            printf("get frame_id: %d size: %d\n", pre_frame_id, shared_info->size);
        }
        if (!semaphore_V(sem_id2))
            exit(EXIT_FAILURE);
        if (!semaphore_V(sem_id))
            exit(EXIT_FAILURE);
    }
    return ((void*) 0);
}

int main() {
    int info_shmid, data_shmid, err_shmid;
    void *info_shm, *data_shm, *err_shm;

    // shared memory
    info_shm = create_shared_memory(SHARE_KEY, sizeof(tran_data), info_shmid);
    data_shm = create_shared_memory(DATA_KEY, sizeof(uint8_t)*DATA_SIZE, data_shmid);
    err_shm = create_shared_memory(ERR_KEY, sizeof(struct area_err), err_shmid);
    shared_info = (struct tran_data*) info_shm;
    shared_data = (uint8_t*) data_shm;
    err_info = (struct area_err*) err_shm;

    // semaphore
    sem_id = semget(SEM_KEY, 1, 0666 | IPC_CREAT);
    sem_id2 = semget(SEM_KEY2, 1, 0666 | IPC_CREAT);
    if (!set_semvalue(sem_id2)) {
            fprintf(stderr, "Failed to init semaphore\n");
            exit(EXIT_FAILURE);
    }

    // camshift
    VideoCapture cap;
    Rect trackWindow;
    int hsize = 16;
    float hranges[] = {0,180};
    const float* phranges = hranges;

    cout << hot_keys;
    namedWindow( "CamShift Demo", WINDOW_AUTOSIZE );
    namedWindow( "Histogram", 0 );
    setMouseCallback( "CamShift Demo", onMouse, 0 );
    createTrackbar( "Vmin", "CamShift Demo", &vmin, 256, 0 );
    createTrackbar( "Vmax", "CamShift Demo", &vmax, 256, 0 );
    createTrackbar( "Smin", "CamShift Demo", &smin, 256, 0 );
    createTrackbar( "ini_area", "CamShift Demo", &ini_area, 80000, 0 );

    Mat frame, hsv, hue, mask, hist, histimg = Mat::zeros(200, 320, CV_8UC3), backproj;
    bool paused = false;

    // thread to load data
    int err = pthread_create(&ntid, NULL, copy_image_thread, NULL);
    if (err != 0)
        exit(EXIT_FAILURE);

    IplImage *src = NULL;
    IplImage *dst = NULL;
    for(;;) {
        if (!semaphore_P(sem_id2))
            exit(EXIT_FAILURE);
        if (pre_frame_id == -1 || data == NULL) {
            if (!semaphore_V(sem_id2))
                exit(EXIT_FAILURE);
            continue;
        }
        if (NULL == realdata)
            realdata = (uint8_t*)malloc(sizeof(uint8_t)*height*width*3);
        rgb565_to_rgb888(data, width, height, realdata);

        printf("processing frame: %d\n", pre_frame_id);
        if (src == NULL)
            src = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
        if (dst == NULL)
            dst = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
        src->imageData = (char*)realdata;
        cvCvtColor(src, dst, CV_RGB2BGR);
        image = cv::cvarrToMat(src);

        if( !paused )
        {
            cvtColor(image, hsv, COLOR_BGR2HSV);
            if( trackObject )
            {
                printf("tracking\n");
                int _vmin = vmin, _vmax = vmax;

                inRange(hsv, Scalar(0, smin, MIN(_vmin,_vmax)),
                        Scalar(180, 256, MAX(_vmin, _vmax)), mask);
                int ch[] = {0, 0};
                hue.create(hsv.size(), hsv.depth());
                mixChannels(&hsv, 1, &hue, 1, ch, 1);

                if( trackObject < 0 )
                {
                    Mat roi(hue, selection), maskroi(mask, selection);
                    calcHist(&roi, 1, 0, maskroi, hist, 1, &hsize, &phranges);
                    normalize(hist, hist, 0, 255, NORM_MINMAX);

                    trackWindow = selection;
                    trackObject = 1;

                    histimg = Scalar::all(0);
                    int binW = histimg.cols / hsize;
                    Mat buf(1, hsize, CV_8UC3);
                    for( int i = 0; i < hsize; i++ )
                        buf.at<Vec3b>(i) = Vec3b(saturate_cast<uchar>(i*180./hsize), 255, 255);
                    cvtColor(buf, buf, COLOR_HSV2BGR);

                    for( int i = 0; i < hsize; i++ )
                    {
                        int val = saturate_cast<int>(hist.at<float>(i)*histimg.rows/255);
                        rectangle( histimg, Point(i*binW,histimg.rows),
                                   Point((i+1)*binW,histimg.rows - val),
                                   Scalar(buf.at<Vec3b>(i)), -1, 8 );
                    }
                }

                calcBackProject(&hue, 1, 0, hist, backproj, &phranges);
                backproj &= mask;
                RotatedRect trackBox = CamShift(backproj, trackWindow,
                                    TermCriteria( TermCriteria::EPS | TermCriteria::COUNT, 10, 1 ));
                if( trackWindow.area() <= 1 )
                {
                    int cols = backproj.cols, rows = backproj.rows, r = (MIN(cols, rows) + 5)/6;
                    trackWindow = Rect(trackWindow.x - r, trackWindow.y - r,
                                       trackWindow.x + r, trackWindow.y + r) &
                                  Rect(0, 0, cols, rows);
                }
                Size rect_size = trackBox.size;
                Point2f center = trackBox.center;
                printf("selectObject%d selection->width:%d, selection->height:%d\n", selectObject, rect_size.width, rect_size.height);
                err_info->x_err = (center.x - width / 2) / 320;
                err_info->y_err = (center.y - height / 2) / 240 * 0.7;
                err_info->z_err = (float)(ini_area - rect_size.width * rect_size.height) / 640 / 480 * 2;
                printf("%f %f %f \n", err_info->x_err, err_info->y_err, err_info->z_err);
                if( backprojMode )
                    cvtColor( backproj, image, COLOR_GRAY2BGR );
                ellipse( image, trackBox, Scalar(0,0,255), 3, 16 );
            }
        }
        else if( trackObject < 0 )
            paused = false;

        if( selectObject && selection.width > 0 && selection.height > 0 )
        {
            Mat roi(image, selection);
            bitwise_not(roi, roi);
        }
        imshow( "CamShift Demo", image );
        imshow( "Histogram", histimg );

        char c = (char)waitKey(10);
        if( c == 27 )
            break;
        switch(c)
        {
        case 'b':
            backprojMode = !backprojMode;
            break;
        case 'c':
            trackObject = 0;
            histimg = Scalar::all(0);
            break;
        case 'h':
            showHist = !showHist;
            if( !showHist )
                destroyWindow( "Histogram" );
            else
                namedWindow( "Histogram", 1 );
            break;
        case 'p':
            paused = !paused;
            break;
        default:
            ;
        }
        printf("end processing\n");
        if (!semaphore_V(sem_id2))
            exit(EXIT_FAILURE);
    }
    void *trect;
    pthread_join(ntid, &trect);
    if (NULL != data) {
        free(data);
        data = NULL;
    }
    if (NULL != realdata) {
        free(realdata);
        realdata = NULL;
    }
    if (shmdt((void*)shared_info) == -1) {
        fprintf(stderr, "shmdt failed\n");
        exit(EXIT_FAILURE);
    }

    if (shmctl(info_shmid, IPC_RMID, 0) == -1) {
        fprintf(stderr, "shmtcl(RMID) failed\n");
        exit(EXIT_FAILURE);
    }

    if (shmdt((void*)shared_data) == -1) {
        fprintf(stderr, "shmdt failed\n");
        exit(EXIT_FAILURE);
    }

    if (shmctl(data_shmid, IPC_RMID, 0) == -1) {
        fprintf(stderr, "shmtcl(RMID) failed\n");
        exit(EXIT_FAILURE);
    }
    del_semaphore(sem_id2);
    return 0;
}
