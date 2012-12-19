/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// WARNING : Excessive logging, if VSYNC_DEBUG enabled
#define VSYNC_DEBUG 0

#include <utils/Log.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <gralloc_priv.h>
#include <fb_priv.h>
#include <linux/msm_mdp.h>
#include "hwc_utils.h"
#include "hwc_external.h"
#include "string.h"

namespace qhwc {

#define HWC_VSYNC_THREAD_NAME "hwcVsyncThread"

static void *vsync_loop(void *param)
{
    const char* vsync_timestamp_fb0 = "/sys/class/graphics/fb0/vsync_event";

    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);
    private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);

    char thread_name[64] = HWC_VSYNC_THREAD_NAME;
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0,
                HAL_PRIORITY_URGENT_DISPLAY + ANDROID_PRIORITY_MORE_FAVORABLE);

    const int MAX_DATA = 64;
    const int MAX_RETRY_COUNT = 100;
    static char vdata[MAX_DATA];

    uint64_t cur_timestamp=0;
    ssize_t len = -1;
    int fb_timestamp = -1; // fb0 file for primary
    int ret = 0;
    bool enabled = false;

    // Open the primary display vsync_event sysfs node
    fb_timestamp = open(vsync_timestamp_fb0, O_RDONLY);
    if (fb_timestamp < 0) {
        ALOGE("FATAL:%s:not able to open file:%s, %s",  __FUNCTION__,
               vsync_timestamp_fb0, strerror(errno));
        return NULL;
    }

    /* Currently read vsync timestamp from drivers
       e.g. VSYNC=41800875994
    */

    hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];

    do {
        pthread_mutex_lock(&ctx->vstate.lock);
        while (ctx->vstate.enable == false) {
            if(enabled) {
                int e = 0;
                if(ioctl(m->framebuffer->fd, MSMFB_OVERLAY_VSYNC_CTRL,
                                                                &e) < 0) {
                    ALOGE("%s: vsync control failed for fb0 enabled=%d : %s",
                                  __FUNCTION__, enabled, strerror(errno));
                    ret = -errno;
                }
                enabled = false;
            }
            pthread_cond_wait(&ctx->vstate.cond, &ctx->vstate.lock);
        }
        pthread_mutex_unlock(&ctx->vstate.lock);

        if (!enabled) {
            int e = 1;
            if(ioctl(m->framebuffer->fd, MSMFB_OVERLAY_VSYNC_CTRL,
                                                            &e) < 0) {
                ALOGE("%s: vsync control failed for fb0 enabled=%d : %s",
                                 __FUNCTION__, enabled, strerror(errno));
                ret = -errno;
            }
            enabled = true;
        }

        for(int i = 0; i < MAX_RETRY_COUNT; i++) {
            len = pread(fb_timestamp, vdata, MAX_DATA, 0);
            if(len < 0 && (errno == EAGAIN ||
                           errno == EINTR  ||
                           errno == EBUSY)) {
                ALOGW("%s: vsync read: %s, retry (%d/%d).",
                      __FUNCTION__, strerror(errno), i, MAX_RETRY_COUNT);
                continue;
            } else {
                break;
            }
        }

        if (len < 0){
            ALOGE("%s:not able to read file:%s, %s", __FUNCTION__,
                   vsync_timestamp_fb0, strerror(errno));
            //XXX: Need to continue here since SF needs vsync signal to compose
            continue;
        }

        // extract timestamp
        const char *str = vdata;
        if (!strncmp(str, "VSYNC=", strlen("VSYNC="))) {
            cur_timestamp = strtoull(str + strlen("VSYNC="), NULL, 0);
        } else {
            ALOGE("FATAL:%s:timestamp data not in correct format",
                                                     __FUNCTION__);
        }
        // send timestamp to HAL
        ALOGD_IF(VSYNC_DEBUG, "%s: timestamp %llu sent to HWC for %s",
              __FUNCTION__, cur_timestamp, "fb0");
        proc->vsync(proc, 0, cur_timestamp);
      // repeat, whatever, you just did
    } while (true);

    if(fb_timestamp > 0)
        close(fb_timestamp);
    return NULL;
}

void init_vsync_thread(hwc_context_t* ctx)
{
    int ret;
    pthread_t vsync_thread;
    ALOGI("Initializing VSYNC Thread");
    ret = pthread_create(&vsync_thread, NULL, vsync_loop, (void*) ctx);
    if (ret) {
        ALOGE("%s: failed to create %s: %s", __FUNCTION__,
            HWC_VSYNC_THREAD_NAME, strerror(ret));
    }
}

}; //namespace
