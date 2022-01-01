#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#define TEMP_PID_PATH "/tmp/brightnessControlProcess.pid"
#define STEEPNESS 50
#define CHANGE_SPEED 0.008

FILE *fp;

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do
    {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

char shouldExit = 0;

void exitHandler(int code)
{
    shouldExit = 1;
}

float getPros(float in)
{
    return (pow(STEEPNESS, in) - 1) / (STEEPNESS - 1);
}

float getProsInv(float in)
{
    return log((STEEPNESS - 1) * in + 1) / log(STEEPNESS);
}

/**
 * returns 1 if file has write permissions for other users than the current effective user
 */
char checkFilePermissions(FILE *file)
{
    struct stat sts;

    if (fstat(fileno(file), &sts) == -1)
    {
        return 1;
    }

    if (sts.st_uid != geteuid())
    {
        printf("Wrong permissions\n");
        return 1;
    }

    if (sts.st_mode & S_IWGRP || sts.st_mode & S_IWOTH)
    {
        printf("Only owner should have write permissions to the pid file\n");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGUSR1, exitHandler);

    if (argc != 2)
    {
        printf("Please specify direction");
        return 1;
    }

    float dir;

    if (strcmp(argv[1], "up") == 0)
    {
        dir = CHANGE_SPEED;
    }
    else if (strcmp(argv[1], "down") == 0)
    {
        dir = -CHANGE_SPEED;
    }
    else if (strcmp(argv[1], "stop") == 0)
    {
        FILE *brightnessPIDFP = fopen(TEMP_PID_PATH, "r");

        if (brightnessPIDFP == NULL || checkFilePermissions(brightnessPIDFP))
        {
            return 1;
        }

        int otherPID;
        if (fscanf(brightnessPIDFP, "%d", &otherPID) == 0)
        {
            return 1;
        }
        fclose(brightnessPIDFP);

        kill(otherPID, SIGUSR1);
        return 0;
    }
    else
    {
        printf("Unknown operation: \"%s\"", argv[1]);
        return 1;
    }

    FILE *brightnessPIDFP = fopen(TEMP_PID_PATH, "r");

    if (brightnessPIDFP != NULL)
    {
        if (checkFilePermissions(brightnessPIDFP))
        {
            printf("removing\n");
            remove(TEMP_PID_PATH);
        }
        else
        {
            int otherPID;
            if (fscanf(brightnessPIDFP, "%d", &otherPID) == 0)
            {
                return 1;
            }
            fclose(brightnessPIDFP);

            struct stat sts;

            char procFilePath[20];
            snprintf(procFilePath, sizeof(procFilePath), "/proc/%d", otherPID);

            if (stat(procFilePath, &sts) == -1 && errno == ENOENT)
            {
                remove(TEMP_PID_PATH); // remove file of pid that doesn't exist
            }
            else
            {
                return 0; // brightness changer is already running
            }
        }
    }

    brightnessPIDFP = fopen(TEMP_PID_PATH, "w");
    if (brightnessPIDFP == NULL)
    {
        perror("errr");
    }
    char brightnessProcessPIDStr[15];
    sprintf(brightnessProcessPIDStr, "%d", getpid());
    fwrite(brightnessProcessPIDStr, sizeof(char), strlen(brightnessProcessPIDStr), brightnessPIDFP);
    fclose(brightnessPIDFP);

    int maxBrightness;
    int currentBrightness;

    fp = fopen("/sys/class/backlight/intel_backlight/max_brightness", "r");

    if (fp == NULL)
    {
        perror("Error opening max_brightness file");
        return 1;
    }

    if (fscanf(fp, "%d", &maxBrightness) != 1)
    {
        printf("Error reading max_brightness file\n");
        return 1;
    }

    fclose(fp);

    fp = fopen("/sys/class/backlight/intel_backlight/brightness", "r+");

    if (fp == NULL)
    {
        perror("Error opening current brightness file");
        return 1;
    }

    if (fscanf(fp, "%d", &currentBrightness) != 1)
    {
        printf("Error reading current brightness file\n");
        return 1;
    }

    float precentage = getProsInv(currentBrightness / (float)maxBrightness);

    while (1)
    {
        precentage += dir;
        if (precentage >= 1)
        {
            precentage = 1;
            shouldExit = 1;
        }
        if (precentage <= 0.0)
        {
            precentage = 0.0;
            shouldExit = 1;
        }

        int writtenBrightness = (int)((float)maxBrightness * getPros(precentage));

        // make sure value written is between 1 and max brightness
        if (writtenBrightness > maxBrightness)
        {
            writtenBrightness = maxBrightness;
        }
        if (writtenBrightness < 1) // leave at 1 for faster rebrightning
        {
            writtenBrightness = 1;
        }

        char brightnessStr[15];
        sprintf(brightnessStr, "%d", writtenBrightness);
        fwrite(brightnessStr, sizeof(char), strlen(brightnessStr), fp);

        if (shouldExit)
            break;
        fp = freopen(NULL, "r+", fp);
        msleep(10);
    }

    fclose(fp);
    remove(TEMP_PID_PATH); // no need for error handling
}