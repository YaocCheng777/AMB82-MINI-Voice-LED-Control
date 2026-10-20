#include <Arduino.h>
#include "NNGestureDetection.h"
#include "SD_Model.h"
#include "VideoStreamOverlay.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "mmf2_module.h"
#include "timers.h"
#include "module_vipnn.h"
#include "model_palm_detection.h"
#include "model_hand_landmark.h"
#include "siso_drv.h"
#include "avcodec.h"
#include "vfs.h"
#include "osd_render.h"
#include "roi_delta_qp/roi_delta_qp.h"

extern int vipnn_control(void *p, int cmd, int arg);

#ifdef __cplusplus
}
#endif

#undef min
#undef max
#include <vector>

#define LIMIT(x, lower, upper) \
    if (x < lower) \
        x = lower; \
    else if (x > upper) \
        x = upper;

std::vector<GestureDetectionResult> NNGestureDetection::hand_result_vector;
void (*NNGestureDetection::GD_user_CB)(std::vector<GestureDetectionResult>);

TimerHandle_t NNGestureDetection::osd_cleanup_timer = NULL;
nn_data_param_t NNGestureDetection::palm_nn_roi = {0};
nn_data_param_t NNGestureDetection::handlandmark_nn_roi = {0};

NNGestureDetection::NNGestureDetection(void)
{
}

NNGestureDetection::~NNGestureDetection(void)
{
    end();
}

void NNGestureDetection::configVideo(VideoSetting &config)
{
    palm_nn_roi.img.width = config._w;
    palm_nn_roi.img.height = config._h;
    palm_nn_roi.img.rgb = 0;
    palm_nn_roi.img.roi.xmin = 0;
    palm_nn_roi.img.roi.xmax = config._w;
    palm_nn_roi.img.roi.ymin = 0;
    palm_nn_roi.img.roi.ymax = config._h;
    palm_nn_roi.codec_type = AV_CODEC_ID_RGB888;

    handlandmark_nn_roi.img.width = 224;
    handlandmark_nn_roi.img.height = 224;
    handlandmark_nn_roi.img.rgb = 0;
    handlandmark_nn_roi.img.roi.xmin = 0;
    handlandmark_nn_roi.img.roi.xmax = 224;
    handlandmark_nn_roi.img.roi.ymin = 0;
    handlandmark_nn_roi.img.roi.ymax = 224;
    handlandmark_nn_roi.codec_type = AV_CODEC_ID_NN_RAW;
}

void NNGestureDetection::configThreshold(float confidence_threshold, float nms_threshold)
{
    pd_confidence_thresh = confidence_threshold;
    pd_nms_thresh = nms_threshold;
}

void NNGestureDetection::begin(void)
{
    // Palm Detection Configuration
    if (_p_mmf_context == NULL) {
        _p_mmf_context = mm_module_open(&vipnn_module);
    }
    if (_p_mmf_context == NULL) {
        printf("\r\n[ERROR] NNPalmDetection init failed\n");
        return;
    }
    if ((palm_nn_roi.img.width == 0) || (palm_nn_roi.img.height == 0)) {
        printf("\r\n[ERROR] NNPalmDetection video not configured\n");
        return;
    }

    if (_nntask != GESTURE_DETECTION) {
        if (ARDUINO_LOAD_MODEL == 0x02) {
            printf("\r\n[INFO] Models loaded using SD Card\n");
        } else {
            while (1) {
                printf("\r\n[ERROR] Invalid NN task selected! Please check modelSelect() again\n");
                delay(5000);
            }
        }
    }

    if (ARDUINO_LOAD_MODEL == 0x02) {
        vfs_init(NULL);    // init filesystem
        vfs_user_register("sd", VFS_FATFS, VFS_INF_SD);
        vipnn_control(_p_mmf_context->priv, CMD_VIPNN_SET_MODEL, (int)&palmdet_from_sd);
    } else {
        vipnn_control(_p_mmf_context->priv, CMD_VIPNN_SET_MODEL, (int)&palm_detection_fwfs);
    }
    vipnn_control(_p_mmf_context->priv, CMD_VIPNN_SET_IN_PARAMS, (int)&palm_nn_roi);
    mm_module_ctrl(_p_mmf_context, CMD_VIPNN_SET_RES_SIZE, sizeof(palmdetect_res_t));    // result size
    mm_module_ctrl(_p_mmf_context, CMD_VIPNN_SET_RES_MAX_CNT, MAX_HAND_DETECT_NUM);      // result max count
    mm_module_ctrl(_p_mmf_context, MM_CMD_SET_DATAGROUP, MM_GROUP_START);
    vipnn_control(_p_mmf_context->priv, CMD_VIPNN_SET_CONFIDENCE_THRES, (int)&pd_confidence_thresh);
    vipnn_control(_p_mmf_context->priv, CMD_VIPNN_SET_NMS_THRES, (int)&pd_nms_thresh);
    mm_module_ctrl(_p_mmf_context, CMD_VIPNN_SET_OUTPUT, 1);    // enable module output
    mm_module_ctrl(_p_mmf_context, MM_CMD_SET_QUEUE_LEN, 1);
    mm_module_ctrl(_p_mmf_context, MM_CMD_INIT_QUEUE_ITEMS, MMQI_FLAG_STATIC);
    vipnn_control(_p_mmf_context->priv, CMD_VIPNN_APPLY, 0);

    // Hand Landmark
    if (handlandmark_ctx == NULL) {
        handlandmark_ctx = mm_module_open(&vipnn_module);
    }
    if (handlandmark_ctx == NULL) {
        printf("\r\n[ERROR] NNHandLandmark init failed\n");
        return;
    }
    if (palm_hand_siso_ctx == NULL) {
        palm_hand_siso_ctx = (void *)sisoCreate();
    }
    if (palm_hand_siso_ctx == NULL) {
        printf("\r\n[ERROR] HandLandmark SISO init failed\n");
        return;
    }

    // Hand Landmark configuration
    if (ARDUINO_LOAD_MODEL == 0x02) {
        vfs_init(NULL);    // init filesystem
        vfs_user_register("sd", VFS_FATFS, VFS_INF_SD);
        vipnn_control(handlandmark_ctx->priv, CMD_VIPNN_SET_MODEL, (int)&handlandmark_from_sd);
    } else {
        vipnn_control(handlandmark_ctx->priv, CMD_VIPNN_SET_MODEL, (int)&hand_landmark_fwfs);
    }

    mm_module_ctrl(handlandmark_ctx, CMD_VIPNN_SET_IN_PARAMS, (int)&handlandmark_nn_roi);
    vipnn_control(handlandmark_ctx->priv, CMD_VIPNN_SET_CASCADE, 2);                     // this module is cascade mode
    vipnn_control(handlandmark_ctx->priv, CMD_VIPNN_SET_OUTPUT, 1);                      // output
    mm_module_ctrl(handlandmark_ctx, CMD_VIPNN_SET_RES_SIZE, sizeof(handland_res_t));    // result size
    mm_module_ctrl(handlandmark_ctx, CMD_VIPNN_SET_RES_MAX_CNT, MAX_HAND_DETECT_NUM);    // result max count

    mm_module_ctrl(handlandmark_ctx, MM_CMD_SET_DATAGROUP, MM_GROUP_END);
    mm_module_ctrl(handlandmark_ctx, MM_CMD_SET_QUEUE_LEN, 1);
    mm_module_ctrl(handlandmark_ctx, MM_CMD_INIT_QUEUE_ITEMS, MMQI_FLAG_STATIC);
    mm_module_ctrl(handlandmark_ctx, CMD_VIPNN_SET_DISPPOST, (int)drawHandObject);
    vipnn_control(handlandmark_ctx->priv, CMD_VIPNN_APPLY, 0);

    // SISO link palm detection to HandLandmark
    sisoRegIn(palm_hand_siso_ctx, _p_mmf_context);
    sisoRegOut(palm_hand_siso_ctx, handlandmark_ctx);
    sisoStart(palm_hand_siso_ctx);
}

void NNGestureDetection::end(void)
{
    if (palm_hand_siso_ctx == NULL) {
        return;
    }
    sisoDestroy(palm_hand_siso_ctx);
    palm_hand_siso_ctx = NULL;

    if (_p_mmf_context == NULL) {
        return;
    }

    if (mm_module_close(_p_mmf_context) == NULL) {
        _p_mmf_context = NULL;
    } else {
        printf("\r\n[ERROR] NNPalmDetection deinit failed\n");
    }

    if (handlandmark_ctx == NULL) {
        return;
    }
    if (mm_module_close(handlandmark_ctx) == NULL) {
        handlandmark_ctx = NULL;
    } else {
        printf("\r\n[ERROR] NNHandLandmark deinit failed\n");
    }
}

void NNGestureDetection::setResultCallback(void (*gd_callback)(std::vector<GestureDetectionResult>))
{
    GD_user_CB = gd_callback;
}

uint16_t NNGestureDetection::getResultCount(void)
{
    uint16_t gesturedetect_res_count = hand_result_vector.size();
    if (gesturedetect_res_count > 128) {
        gesturedetect_res_count = 128;
    }
    return gesturedetect_res_count;
}

GestureDetectionResult NNGestureDetection::getResult(uint16_t index)
{
    if (index >= hand_result_vector.size()) {
        return GestureDetectionResult();
    }
    return hand_result_vector[index];
}

std::vector<GestureDetectionResult> NNGestureDetection::getResult(void)
{
    return hand_result_vector;
}


// Week 2: count all five fingers, including thumb, from landmarks.
// This leaves the Realtek VIPNN model initialization unchanged.
static float quizDist(const landmark3d_t &p, int a, int b)
{
    float dx = p.pos[a].x - p.pos[b].x;
    float dy = p.pos[a].y - p.pos[b].y;
    return sqrtf(dx * dx + dy * dy);
}

static float quizAngle(const landmark3d_t &p, int a, int b, int c)
{
    float ux = p.pos[a].x - p.pos[b].x;
    float uy = p.pos[a].y - p.pos[b].y;
    float vx = p.pos[c].x - p.pos[b].x;
    float vy = p.pos[c].y - p.pos[b].y;
    float denom = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
    if (denom < 0.001f) return 0.0f;
    float cosine = (ux * vx + uy * vy) / denom;
    if (cosine < -1.0f) cosine = -1.0f;
    if (cosine > 1.0f) cosine = 1.0f;
    return acosf(cosine) * 57.2957795f;
}

static int quizCountFingers(const landmark3d_t &p)
{
    // Four non-thumb fingers are checked using joint angles (unchanged).
    const int mcp[4] = {5, 9, 13, 17};
    int count = 0;

    // Thumb: compare tip-to-palm distances with thumb MCP-to-palm distances.
    // Unlike thumb joint angles, these ratios also work when the thumb opens
    // sideways (as in a fully spread open hand).
    float indexBase = quizDist(p, 2, 5);
    float pinkyBase = quizDist(p, 2, 17);
    float indexRatio = quizDist(p, 4, 5) / (indexBase > 0.001f ? indexBase : 0.001f);
    float pinkyRatio = quizDist(p, 4, 17) / (pinkyBase > 0.001f ? pinkyBase : 0.001f);
    bool thumbExtended = (indexRatio > 1.25f && pinkyRatio > 1.35f);
    if (thumbExtended) ++count;
    for (int i = 0; i < 4; ++i) {
        int m = mcp[i];
        float pipAngle = quizAngle(p, m, m + 1, m + 2);
        float dipAngle = quizAngle(p, m + 1, m + 2, m + 3);
        float tipFromWrist = quizDist(p, 0, m + 3);
        float pipFromWrist = quizDist(p, 0, m + 1);
        if (pipAngle > 150.0f && dipAngle > 145.0f && tipFromWrist > pipFromWrist * 1.12f) {
            ++count;
        }
    }
    return count;
}

static void quizStroke(int x1, int y1, int x2, int y2)
{
    OSD.drawLine(RTSP_CHANNEL, x1, y1, x2, y2, 14, COLOR_GREEN, HAND_LINK_LAYER);
}

// 5x7 stroke font for control commands; uses the same overlay bitmap as A-D.
static void quizDrawWord(const char *word)
{
    // 5x7 bitmap glyphs for C,O,N,F,I,R,M,E,X,T.
    struct Glyph { char ch; unsigned char row[7]; };
    static const Glyph font[] = {
        {'C',{14,17,16,16,16,17,14}},
        {'O',{14,17,17,17,17,17,14}},
        {'N',{17,25,21,19,17,17,17}},
        {'F',{31,16,16,30,16,16,16}},
        {'I',{31,4,4,4,4,4,31}},
        {'R',{30,17,17,30,20,18,17}},
        {'M',{17,27,21,21,17,17,17}},
        {'E',{31,16,16,30,16,16,31}},
        {'X',{17,17,10,4,10,17,17}},
        {'T',{31,4,4,4,4,4,4}}
    };
    const int ox = 390, oy = 95, scale = 5, step = 34;
    for (int c = 0; word[c]; ++c) {
        const Glyph *g = NULL;
        for (unsigned int k = 0; k < sizeof(font)/sizeof(font[0]); ++k) {
            if (font[k].ch == word[c]) { g = &font[k]; break; }
        }
        if (!g) continue;
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 5; ++x) {
                if (g->row[y] & (1 << (4-x))) {
                    int px = ox + c * step + x * scale;
                    int py = oy + y * scale;
                    OSD.drawLine(RTSP_CHANNEL, px, py, px + scale-1, py + scale-1, 3, COLOR_GREEN, HAND_LINK_LAYER);
                }
            }
        }
    }
}

static void quizDrawLetter(int fingers)
{
    // Draw large A/B/C/D using the already-working OSD line API.
    const int x = 500, y = 100, w = 120, h = 160;
    if (fingers == 1) { // A
        quizStroke(x, y + h, x + w/2, y);
        quizStroke(x + w/2, y, x + w, y + h);
        quizStroke(x + w/4, y + h/2, x + 3*w/4, y + h/2);
    } else if (fingers == 2) { // B
        quizStroke(x, y, x, y + h);
        quizStroke(x, y, x + 3*w/4, y);
        quizStroke(x + 3*w/4, y, x + w, y + h/4);
        quizStroke(x + w, y + h/4, x + 3*w/4, y + h/2);
        quizStroke(x, y + h/2, x + 3*w/4, y + h/2);
        quizStroke(x + 3*w/4, y + h/2, x + w, y + 3*h/4);
        quizStroke(x + w, y + 3*h/4, x + 3*w/4, y + h);
        quizStroke(x + 3*w/4, y + h, x, y + h);
    } else if (fingers == 3) { // C
        quizStroke(x + w, y, x + w/4, y);
        quizStroke(x + w/4, y, x, y + h/4);
        quizStroke(x, y + h/4, x, y + 3*h/4);
        quizStroke(x, y + 3*h/4, x + w/4, y + h);
        quizStroke(x + w/4, y + h, x + w, y + h);
    } else if (fingers == 4) { // D
        quizStroke(x, y, x, y + h);
        quizStroke(x, y, x + w/2, y);
        quizStroke(x + w/2, y, x + w, y + h/4);
        quizStroke(x + w, y + h/4, x + w, y + 3*h/4);
        quizStroke(x + w, y + 3*h/4, x + w/2, y + h);
        quizStroke(x + w/2, y + h, x, y + h);
    }
}

// AI inference callback: retain gesture classification and Serial messages,
// but do not draw any skeleton, text, boxes or other OSD graphics on RTSP.
void NNGestureDetection::drawHandObject(void *p, void *img_param)
{
    if (!p || !img_param) {
        return;
    }
    vipnn_out_buf_t *out = (vipnn_out_buf_t *)p;
    handland_res_t *res = (handland_res_t *)&out->res[0];
    if (res->handedness == HANDEDNESS_NOTFOUND) {
        return;
    }
    if (fixLandmark(res) < 0) {
        return;
    }
    const int quizFingers = quizCountFingers(res->landmark3d);
    static unsigned long lastQuizLog = 0;
    const unsigned long nowQuiz = millis();
    if (nowQuiz - lastQuizLog >= 1000) {
        lastQuizLog = nowQuiz;
        printf("[QUIZ] fingers=%d, letter=%c\r\n", quizFingers,
               (quizFingers >= 1 && quizFingers <= 4) ? ('A' + quizFingers - 1) : '-');
    }
}

/*
Correcting coordinates
After performing a series of rotations, scaling, and translations, the coordinates need to be corrected back.
For example, if you previously rotated the hand to point the middle finger upwards, you need to rotate it back here; otherwise, the drawn hand will always have the middle finger pointing up.
*/
int NNGestureDetection::fixLandmark(handland_res_t *lm)
{
    float cx = (float)handlandmark_nn_roi.img.width / 2.0;
    float cy = (float)handlandmark_nn_roi.img.height / 2.0;
    float cos_theta = cosf(lm->theta);
    float sin_theta = sinf(lm->theta);
    float scale = lm->w / (float)(handlandmark_nn_roi.img.width / 2.0) / lm->ratio;
    for (int i = 0; i < HAND_LANDMARK_NUM; i++) {
        // printf("before:%d x = %f, y = %f\r\n", i, lm->landmark3d.pos[i].x, lm->landmark3d.pos[i].y);
        // Place your hand at the edge of the screen. Sometimes, the landmark will go out of the frame. Let's correct it here first.
        LIMIT(lm->landmark3d.pos[i].x, 0, handlandmark_nn_roi.img.width);
        LIMIT(lm->landmark3d.pos[i].y, 0, handlandmark_nn_roi.img.height);
        float dx = lm->landmark3d.pos[i].x - cx;
        float dy = lm->landmark3d.pos[i].y - cy;
        float x = dx * cos_theta - dy * sin_theta + cx;
        float y = dx * sin_theta + dy * cos_theta + cy;
        if (x < 0 || y < 0) {
            // Place your hand at the edge of the screen. After calibration, it might extend far beyond the screen, so let's not draw this for now.
            printf("error! x = %f, y = %f\r\n", x, y);
            return -1;
        }
        x = x * scale;
        y = y * scale;
        lm->landmark3d.pos[i].x = x + lm->offset_x;
        lm->landmark3d.pos[i].y = y + lm->offset_y;
        // printf("after:%d x = %f, y = %f\r\n", i, lm->landmark3d.pos[i].x, lm->landmark3d.pos[i].y);

        // Here are some additional adjustments to make the skeleton more accurate when the hand is in the upper corner of the screen.
        if (lm->offset_x < 20) {
            lm->landmark3d.pos[i].x = lm->landmark3d.pos[i].x - (20 - lm->offset_x);
            LIMIT(lm->landmark3d.pos[i].x, 0, handlandmark_nn_roi.img.width);
        }
        if (lm->offset_y < 20) {
            lm->landmark3d.pos[i].y = lm->landmark3d.pos[i].y - (20 - lm->offset_y);
            LIMIT(lm->landmark3d.pos[i].y, 0, handlandmark_nn_roi.img.height);
        }
    }
    // printf("lm->offset_x = %d, lm->offset_y = %d\r\n", lm->offset_x, lm->offset_y);
    return 0;
}

void NNGestureDetection::handCleanupCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    OSD.createBitmap(RTSP_CHANNEL, HAND_JOINT_LAYER);
    OSD.createBitmap(RTSP_CHANNEL, HAND_LINK_LAYER);
    OSD.update(RTSP_CHANNEL, HAND_JOINT_LAYER, 0);
    OSD.update(RTSP_CHANNEL, HAND_LINK_LAYER, 1);
}

void NNGestureDetection::drawHandRegion(void)
{
    // draw hand detect region
    OSD.createBitmap(RTSP_CHANNEL, HAND_DETECT_REGION_LAYER);
    OSD.drawRect(RTSP_CHANNEL, (RTSP_WIDTH - RTSP_HEIGHT) / 2, 5, RTSP_WIDTH - (RTSP_WIDTH - RTSP_HEIGHT) / 2, RTSP_HEIGHT - 5, 3,
                 COLOR_BLUE, HAND_DETECT_REGION_LAYER);
    OSD.update(RTSP_CHANNEL, HAND_DETECT_REGION_LAYER, 1);
    osd_cleanup_timer = xTimerCreate("OSD clean timer", 1000 / portTICK_PERIOD_MS, pdTRUE, NULL, handCleanupCallback);
}
