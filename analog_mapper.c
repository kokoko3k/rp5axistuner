
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/time.h>

#define DEADZONE_STICKS 0
#define DEADZONE_TRIGS 0

#define DEBUG 0.0


float apply_curve1D(float val, float curve_power) {
    float normalized = val / 32767.0f;
    float sign = 1.0; 
    if (normalized < 0.0) sign = -1.0;
    float curved = sign * pow(fabsf(normalized), curve_power) ;  // x^ mantenendo il segno
    return curved * 32767.0f;
}

int apply_deadzone(int value, int dz) {
    if (abs(value) < dz)
        return 0;
    
    if (value > 0) {
        // scala lineare da [DEADZONE..32767] a [0..32767]
        return (value - dz) * 32767.0f / (32767.0f - dz);
    } else {
        // scala lineare da [-32767..-DEADZONE] a [-32767..0]
        return (value + dz) * 32767.0f / (32767.0f - dz);
    }
}

static inline int clamp(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}


float mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }


void apply_curve2D(float *x, float *y, float curve_power, float f_compensation) {
    
    if (DEBUG == 1.0) printf("Input raw: x=%d y=%d\n", (int)*x, (int)*y);

    //normalize to -1 1
        float fx = *x;
        float fy = *y;
        fx/=32767.0f;
        fy/=32767.0f;

 
    if (DEBUG == 1.0) printf("input raw norm fx/fy: %f %f\n", fx, fy);        
    

    //hack/compensation to let diagonal reach full +/-1 +/-1 scale
        float abs_diff = fabsf(fabsf(fx) - fabsf(fy));
        float compensation=(1-abs_diff);
        compensation=1+compensation*f_compensation;
        fx*=compensation;
        fy*=compensation;
      
    
    //compute magnitude, apply curve to magnitude.
        float magnitude_in = sqrtf(fx*fx+fy*fy);
        float magnitude_out = pow(magnitude_in, curve_power);
    
    //avoid division by 0.0
        if (magnitude_in > 0.0f) {
            fx = fx*(magnitude_out)/magnitude_in;
            fy = fy*(magnitude_out)/magnitude_in;
        }
    
    if (DEBUG == 1.0) printf("fx/fy: %f %f\n", fx, fy);    
    
            
    //to be safe:
        fx = clampf(fx, -1.0f, 1.0f);
        fy = clampf(fy, -1.0f, 1.0f);


        
    //back to full range:
        fx*=32767.0f;
        fy*=32767.0f;    
        *x = fx;
        *y = fy;
    
    
    if (DEBUG == 1.0) printf("Output curved: x=%d y=%d\n", (int)*x, (int)*y);
}


void emit_event(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, NULL);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

int setup_uinput_device(const char *devname) {
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        perror("Error opening /dev/uinput");
        exit(EXIT_FAILURE);
    }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_ABS);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);

    for (int code = 0; code < KEY_MAX; code++)
        ioctl(ufd, UI_SET_KEYBIT, code);

    int abs_codes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y, ABS_BRAKE, ABS_GAS };
    for (int i = 0; i < sizeof(abs_codes)/sizeof(abs_codes[0]); i++)
        ioctl(ufd, UI_SET_ABSBIT, abs_codes[i]);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", devname);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    //set axis ranges
    for (int i = 0; i < ABS_MAX; i++) {
        uidev.absmin[i] = -32768;
        uidev.absmax[i] = 32767;
    }

    //set trigggers ranges
    uidev.absmin[ABS_BRAKE] = 0;
    uidev.absmax[ABS_BRAKE] = 32767;
    uidev.absmin[ABS_GAS] = 0;
    uidev.absmax[ABS_GAS] = 32767;
    
    //set dpad ranges
    uidev.absmin[ABS_HAT0X] = -1;
    uidev.absmax[ABS_HAT0X] = 1;
    uidev.absmin[ABS_HAT0Y] = -1;
    uidev.absmax[ABS_HAT0Y] = 1;

    write(ufd, &uidev, sizeof(uidev));
    ioctl(ufd, UI_DEV_CREATE);
    sleep(1);
    return ufd;
}



int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Use: %s <device_input> <virtual device name> <curve_power stick> <to square shape factor> <curve power triggers>\n", argv[0]);
        return 1;
    }
    const char *src_dev = argv[1];
    const char *virt_name = argv[2];
    float curve_power_stick = strtof(argv[3], NULL);
    float diag_compensation = strtof(argv[4], NULL);
    float curve_power_trig = strtof(argv[5], NULL);

    
    int src_fd = open(src_dev, O_RDONLY);
    if (src_fd < 0) {
        perror("Error opening source device");
        return 1;
    }

    if (ioctl(src_fd, EVIOCGRAB, 1) < 0) {
        perror("Error EVIOCGRAB");
        return 1;
    }
    
    int ufd = setup_uinput_device(virt_name);
    int abs_x = 0, abs_y = 0, abs_z = 0, abs_rz = 0;
    struct input_event ev;
    while (1) {
        ssize_t rb = read(src_fd, &ev, sizeof(ev));
        if (rb != sizeof(ev)) continue;
        if (ev.type == EV_ABS) {
            int value = ev.value;
            
            //dpad:
            if (ev.code==ABS_HAT0Y||ev.code==ABS_HAT0X) {
                emit_event(ufd, EV_ABS, ev.code, value);
                continue;
            }            
            
            //Analogue triggers:
            if (ev.code==ABS_GAS||ev.code==ABS_BRAKE) {
                value = (int)apply_curve1D((float)value, curve_power_trig);
                emit_event(ufd, EV_ABS, ev.code, value);
                continue;
            }
            
            //Analog sticks:
                //buffer axis values because apply_curve2D() needs both
                //(we need both to apply a curve to the magnitude)
                if (ev.code==ABS_X || ev.code==ABS_Y || ev.code==ABS_Z || ev.code==ABS_RZ) {
                    value = apply_deadzone(value, DEADZONE_STICKS);
                    if (ev.code==ABS_X)   abs_x = value;
                    if (ev.code==ABS_Y)   abs_y = value;
                    if (ev.code==ABS_Z)   abs_z = value;
                    if (ev.code==ABS_RZ)  abs_rz = value;
                }
        }
        //digital / keys:
        else if (ev.type == EV_KEY) {
            emit_event(ufd, EV_KEY, ev.code, ev.value);
        }
        //sync:
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            //if (changed_x || changed_y || changed_z || changed_rz) {
            float fx = (float)abs_x;
            float fy = (float)abs_y;
            float fz = (float)abs_z;
            float frz= (float)abs_rz;
            apply_curve2D(&fx, &fy, curve_power_stick, diag_compensation);
            apply_curve2D(&fz, &frz, curve_power_stick, diag_compensation);
            emit_event(ufd, EV_ABS, ABS_X, (int)roundf(fx));
            emit_event(ufd, EV_ABS, ABS_Y, (int)roundf(fy));
            emit_event(ufd, EV_ABS, ABS_Z, (int)roundf(fz));
            emit_event(ufd, EV_ABS, ABS_RZ, (int)roundf(frz));
            //}
            
            emit_event(ufd, EV_SYN, SYN_REPORT, 0);
        }
    }
    ioctl(ufd, UI_DEV_DESTROY);
    close(src_fd);
    close(ufd);
    return 0;
}
