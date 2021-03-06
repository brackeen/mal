/**
 Plays silent audio in various situations.

 The screen color means:
 * Gray: Test running.
 * Red: Test failed.
 * Green: Test passed (and repeating).

 The tests run repeatedly to help expose issues that may take some time to appear (thread races).

 See "testFunctions[]" to see the list of tests.

 On macOS, while the test is running, test audio service restart with:
 
     sudo killall coreaudiod

 */

#if defined(MAL_EXAMPLE_WITH_GLFM)
#include "glfm.h"
#elif defined(MAL_EXAMPLE_WITH_GLFW)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#else
#error Mal example has no window layer
#endif

#include "mal.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define FILE_COMPAT_ANDROID_ACTIVITY glfmAndroidGetActivity()
#include "file_compat.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static int64_t time_us(void) {
    FILETIME filetime;
    ULARGE_INTEGER large_int;

    GetSystemTimeAsFileTime(&filetime);
    large_int.LowPart = filetime.dwLowDateTime;
    large_int.HighPart = filetime.dwHighDateTime;
    return large_int.QuadPart / 10; // Convert from 100-nanosecond intervals to 1 us intervals
}

static int usleep(unsigned long useconds) {
    if (useconds >= 1000000) {
        errno = EINVAL;
        return -1;
    } else {
        Sleep(useconds / 1000);
        return 0;
    }
}

#else
#include <sys/time.h>
#include <unistd.h>

static int64_t time_us(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (int64_t)t.tv_sec * 1000000 + (int64_t)t.tv_usec;
}

#endif // _WIN32

// MARK: Test

#define kNumPlayers 8

typedef enum {
    STATE_TESTING = 0,
    STATE_FAIL,
    STATE_SUCCESS
} State;

typedef struct {
    const char *name;
    State state;
    size_t failureLine;
    char failureReason[80];
} TestFunctionState;

#define fail(functionState) do { \
    functionState.state = STATE_FAIL; \
    functionState.failureLine = __LINE__; \
} while (0)

#define failWithReason(functionState, reason, ...) do { \
    functionState.state = STATE_FAIL; \
    functionState.failureLine = __LINE__; \
    snprintf((&functionState)->failureReason, sizeof((&functionState)->failureReason), \
             reason, __VA_ARGS__); \
} while (0)

#define TestFunctionStateNew(testName) \
    (TestFunctionState) { \
        .name = testName, .state = STATE_TESTING, .failureLine = 0, .failureReason = { 0 } \
    }

typedef enum {
    PLAYER_ACTION_STOP = 0,
    PLAYER_ACTION_DELETE_PLAYER,
    PLAYER_ACTION_DELETE_BUFFER,
    PLAYER_ACTION_STOP_AND_CLEAR_BUFFER,
    PLAYER_ACTION_STOP_AND_DELETE_PLAYER,
    PLAYER_ACTION_STOP_AND_PLAY,
    PLAYER_ACTION_PAUSE_AND_RESUME,
    PLAYER_ACTION_PAUSE_IMMEDIATELY,
    PLAYER_ACTION_EXIT_LOOP
} PlayerAction;

typedef struct {
    MalContext *context;
    void *bufferData;
    uint32_t bufferDataFrames;
    MalBuffer *buffer;
    MalBuffer *shortBuffer;
    MalBuffer *mediumBuffer;
    MalBuffer *tempBuffers[kNumPlayers];
    MalPlayer *players[kNumPlayers];
    size_t finishedPlayers[kNumPlayers];
    MalFormat format;
    State state;
    int64_t startTime;
    size_t successCount;
    size_t currentTest;
    size_t testIteration;

    GLuint program;
    GLuint vertexBuffer;
    GLuint vertexArray;
    size_t drawIteration;
} StressTestApp;

typedef TestFunctionState (*TestFunction)(StressTestApp *);

static GLuint compileShader(GLenum type, const char *shaderSource) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == 0) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 0) {
            GLchar *log = malloc(logLength);
            glGetShaderInfoLog(shader, logLength, &logLength, log);
            if (log[0] != 0) {
                printf("Shader log: %s\n", log);
            }
            free(log);
        }
        glDeleteShader(shader);
        shader = 0;
    }
    return shader;
}

static void onFinished(MalPlayer *player, void *userData) {
    StressTestApp *app = userData;
    for (size_t i = 0; i < kNumPlayers; i++) {
        if (app->players[i] == player) {
            app->finishedPlayers[i]++;
        }
    }
    // Call a function to make sure the player object is not a dangling pointer
    malPlayerGetState(player);
    if (app->state == STATE_FAIL) {
        printf("onFinished callback occured when state is STATE_FAIL\n");
    }
}

static void clearFinished(StressTestApp *app) {
    for (size_t i = 0; i < kNumPlayers; i++) {
        app->finishedPlayers[i] = 0;
    }
}

static bool allFinished(const StressTestApp *app) {
    for (size_t i = 0; i < kNumPlayers; i++) {
        if (app->finishedPlayers[i] != 1) {
            return false;
        }
    }
    return true;
}

static bool noneFinished(const StressTestApp *app) {
    for (size_t i = 0; i < kNumPlayers; i++) {
        if (app->finishedPlayers[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool createPlayersIfNeeded(StressTestApp *app) {
    for (int i = 0; i < kNumPlayers; i++) {
        if (!app->players[i]) {
            app->players[i] = malPlayerCreate(app->context, app->format);
            if (!app->players[i]) {
                printf("Error: Couldn't create audio player (%i of %i)\n", (i + 1), kNumPlayers);
                return false;
            }
        }
    }
    return true;
}

static bool allPlayersStopped(StressTestApp *app) {
    for (int i = 0; i < kNumPlayers; i++) {
        if (malPlayerGetState(app->players[i]) != MAL_PLAYER_STATE_STOPPED) {
            return false;
        }
    }
    return true;
}

static const char *malPlayerStateString(MalPlayer *player) {
    if (!player) {
        return "(null)";
    }
    switch (malPlayerGetState(player)) {
        case MAL_PLAYER_STATE_STOPPED: default: return "stopped";
        case MAL_PLAYER_STATE_PAUSED: return "paused";
        case MAL_PLAYER_STATE_PLAYING: return "playing";
    }
}

static void printPlayerStates(StressTestApp *app) {
    for (int i = 0; i < kNumPlayers; i++) {
        printf("Player %i: %s finished=%i\n", i, malPlayerStateString(app->players[i]),
               (int)app->finishedPlayers[i]);
    }
}

static bool playerAction(StressTestApp *app, PlayerAction action, size_t index) {
    switch (action) {
        case PLAYER_ACTION_STOP: default: {
            return malPlayerSetState(app->players[index], MAL_PLAYER_STATE_STOPPED);
        }
        case PLAYER_ACTION_DELETE_PLAYER: {
            malPlayerRelease(app->players[index]);
            app->players[index] = NULL;
            return true;
        }
        case PLAYER_ACTION_DELETE_BUFFER: {
            malBufferRelease(app->tempBuffers[index]);
            app->tempBuffers[index] = NULL;
            return malPlayerSetBuffer(app->players[index], NULL);
        }
        case PLAYER_ACTION_STOP_AND_CLEAR_BUFFER: {
            return (malPlayerSetState(app->players[index], MAL_PLAYER_STATE_STOPPED) &&
                    malPlayerSetBuffer(app->players[index], NULL));
        }
        case PLAYER_ACTION_STOP_AND_DELETE_PLAYER: {
            bool success = malPlayerSetState(app->players[index], MAL_PLAYER_STATE_STOPPED);
            malPlayerRelease(app->players[index]);
            app->players[index] = NULL;
            return success;
        }
        case PLAYER_ACTION_STOP_AND_PLAY: {
            bool success = malPlayerSetState(app->players[index], MAL_PLAYER_STATE_STOPPED);
            if (success) {
                success = malPlayerSetState(app->players[index], MAL_PLAYER_STATE_PLAYING);
            }
            return success;
        }
        case PLAYER_ACTION_PAUSE_AND_RESUME: {
            bool success = malPlayerSetState(app->players[index], MAL_PLAYER_STATE_PAUSED);
            if (success) {
                success = malPlayerSetState(app->players[index], MAL_PLAYER_STATE_PLAYING);
            }
            return success;
        }
        case PLAYER_ACTION_PAUSE_IMMEDIATELY: {
            return malPlayerSetState(app->players[index], MAL_PLAYER_STATE_PLAYING);
        }
        case PLAYER_ACTION_EXIT_LOOP: {
            return malPlayerSetLooping(app->players[index], false);
        }
    }
}

static TestFunctionState testDelayedPlayerAction(StressTestApp *app, const char *testName,
                                                 PlayerAction action) {
    static const size_t numPlayers = 8;
    assert(kNumPlayers >= numPlayers);

    TestFunctionState functionState = TestFunctionStateNew(testName);

    size_t lastActionIteration = 16;
    size_t lastIteration = (action == PLAYER_ACTION_EXIT_LOOP ||
                            action == PLAYER_ACTION_STOP_AND_PLAY ||
                            action == PLAYER_ACTION_PAUSE_AND_RESUME ||
                            action == PLAYER_ACTION_PAUSE_IMMEDIATELY) ? 300 : 20;

    bool success;
    if (app->testIteration == 0) {
        clearFinished(app);

        // Create temp buffers
        if (action == PLAYER_ACTION_DELETE_BUFFER) {
            for (size_t i = 0; i < numPlayers; i++) {
                if (!app->tempBuffers[i]) {
                    app->tempBuffers[i] = malBufferCreate(app->context, app->format,
                                                          app->bufferDataFrames, app->bufferData);
                    if (!app->tempBuffers[i]) {
                        failWithReason(functionState,
                                       "Couldn't create temp audio buffer (index %zu)", i);
                        return functionState;
                    }
                }
            }
        }

        // Set callbacks
        for (size_t i = 0; i < numPlayers; i++) {
            if (malPlayerGetState(app->players[i]) != MAL_PLAYER_STATE_STOPPED) {
                failWithReason(functionState, "All players should be stopped (index %zu)", i);
                return functionState;
            }

            malPlayerSetFinishedFunc(app->players[i], onFinished, app);
        }

        // Set buffers
        for (size_t i = 0; i < numPlayers; i++) {
            MalBuffer *buffer;
            if (action == PLAYER_ACTION_DELETE_BUFFER) {
                buffer = app->tempBuffers[i];
            } else if (action == PLAYER_ACTION_EXIT_LOOP) {
                buffer = app->shortBuffer;
            } else if (action == PLAYER_ACTION_STOP_AND_PLAY ||
                       action == PLAYER_ACTION_PAUSE_AND_RESUME ||
                       action == PLAYER_ACTION_PAUSE_IMMEDIATELY) {
                buffer = app->mediumBuffer;
            } else {
                buffer = app->buffer;
            }
            success = malPlayerSetBuffer(app->players[i], buffer);
            if (!success) {
                fail(functionState);
                return functionState;
            }
            success = malPlayerSetLooping(app->players[i], action == PLAYER_ACTION_EXIT_LOOP);
            if (!success) {
                fail(functionState);
                return functionState;
            }
        }

        // Quick test with millisecond delay
        for (size_t i = 0; i < numPlayers/2; i++) {
            success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            if (!success) {
                fail(functionState);
                return functionState;
            }
            if (action == PLAYER_ACTION_PAUSE_IMMEDIATELY) {
                success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PAUSED);
                if (!success) {
                    fail(functionState);
                    return functionState;
                }
            }
            if (i > 0) {
                usleep((1 << (i - 1)) * 1000);
            }
            success = playerAction(app, action, i);
            if (!success) {
                fail(functionState);
                return functionState;
            }
        }

        // Start remaining players
        for (size_t i = numPlayers/2; i < numPlayers; i++) {
            success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            if (!success) {
                failWithReason(functionState, "Couldn't play player %zu", i);
                return functionState;
            }
            if (action == PLAYER_ACTION_PAUSE_IMMEDIATELY) {
                success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PAUSED);
                if (!success) {
                    failWithReason(functionState, "Couldn't pause player %zu", i);
                    return functionState;
                }
            }
        }
    } else {
        // Start/stop after a few frames
        if (app->testIteration == 2) {
            success = playerAction(app, action, 4);
        } else if (app->testIteration == 4) {
            success = playerAction(app, action, 5);
        } else if (app->testIteration == 8) {
            success = playerAction(app, action, 6);
        } else if (app->testIteration == lastActionIteration) {
            success = playerAction(app, action, 7);
        } else if (app->testIteration == 20) {
            success = true;
            if (action == PLAYER_ACTION_STOP) {
                for (size_t i = 0; i < numPlayers; i++) {
                    if (malPlayerGetState(app->players[i]) != MAL_PLAYER_STATE_STOPPED) {
                        success = false;
                        break;
                    }
                }
            }
        } else {
            // Perform an action during the delay (set gain)
            for (size_t i = 0; i < numPlayers; i++) {
                if (malPlayerGetState(app->players[i]) == MAL_PLAYER_STATE_PLAYING) {
                    float gain = 10.0f * ((app->testIteration % 10) + 1);
                    malPlayerSetGain(app->players[i], gain);
                }
            }
            if (action == PLAYER_ACTION_EXIT_LOOP ||
                action == PLAYER_ACTION_STOP_AND_PLAY ||
                action == PLAYER_ACTION_PAUSE_AND_RESUME ||
                action == PLAYER_ACTION_PAUSE_IMMEDIATELY) {

                success = (app->testIteration < lastIteration);

                if (app->testIteration > lastActionIteration && allFinished(app) &&
                    allPlayersStopped(app)) {
                    functionState.state = STATE_SUCCESS;
                    return functionState;
                }
            } else {
                success = (app->testIteration < lastIteration && noneFinished(app));
            }
        }
    }
    if (!success) {
        functionState.state = STATE_FAIL;
    } else if (app->testIteration == lastIteration) {
        functionState.state = STATE_SUCCESS;
    } else {
        functionState.state = STATE_TESTING;
    }
    return functionState;
}

static TestFunctionState testOnFinishedCallback(StressTestApp *app) {
    TestFunctionState functionState = TestFunctionStateNew(__FUNCTION__);
    if (app->testIteration == 0) {
        clearFinished(app);
        for (int i = 0; i < kNumPlayers; i++) {
            malPlayerSetFinishedFunc(app->players[i], onFinished, app);
            bool success = malPlayerSetBuffer(app->players[i], app->shortBuffer);
            if (!success) {
                fail(functionState);
                break;
            }
            success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
            if (!success) {
                fail(functionState);
                break;
            }
        }
    } else {
        if (allFinished(app)) {
            // All callbacks occured, wait until players are actually in the stopped state
            // (The may not be required?)
            if (allPlayersStopped(app)) {
                functionState.state = STATE_SUCCESS;
            }
        } else {
            bool success = (app->testIteration < 100);
            if (!success) {
                fail(functionState);
            }
        }
    }
    return functionState;
}

static TestFunctionState testPlayRepeatedly(StressTestApp *app) {
    TestFunctionState functionState = TestFunctionStateNew(__FUNCTION__);
    if (app->testIteration == 0) {
        for (int i = 0; i < kNumPlayers; i++) {
            malPlayerSetFinishedFunc(app->players[i], NULL, NULL);
            bool success = malPlayerSetBuffer(app->players[i], app->shortBuffer);
            if (!success) {
                fail(functionState);
                return functionState;
            }
        }
    }
    if (app->testIteration < 80) {
        // Play all sounds not playing. Try for 8ms.
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < kNumPlayers; i++) {
                if (malPlayerGetState(app->players[i]) != MAL_PLAYER_STATE_PLAYING) {
                    bool success = malPlayerSetState(app->players[i], MAL_PLAYER_STATE_PLAYING);
                    if (!success) {
                        fail(functionState);
                        return functionState;
                    }
                }
            }
            usleep(1000);
        }
    } else if (app->testIteration < 150) {
        // Wait until finished
        if (allPlayersStopped(app)) {
            functionState.state = STATE_SUCCESS;
        }
    } else {
        fail(functionState);
    }

    return functionState;
}

static TestFunctionState testStartStopWhilePlaying(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_STOP);
}

static TestFunctionState testDeletePlayerWhilePlaying(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_DELETE_PLAYER);
}

static TestFunctionState testDeleteBufferWhilePlaying(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_DELETE_BUFFER);
}

static TestFunctionState testStopAndClearBuffer(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_STOP_AND_CLEAR_BUFFER);
}

static TestFunctionState testStopAndDeletePlayer(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_STOP_AND_DELETE_PLAYER);
}

static TestFunctionState testStopAndPlayAgain(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_STOP_AND_PLAY);
}

static TestFunctionState testPauseAndResume(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_PAUSE_AND_RESUME);
}

static TestFunctionState testImmediatePause(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_PAUSE_IMMEDIATELY);
}

static TestFunctionState testExitLoop(StressTestApp *app) {
    return testDelayedPlayerAction(app, __FUNCTION__, PLAYER_ACTION_EXIT_LOOP);
}

static TestFunction testFunctions[] = {
    testPlayRepeatedly,
    testOnFinishedCallback,
    testStartStopWhilePlaying,
    testDeletePlayerWhilePlaying,
    testDeleteBufferWhilePlaying,
    testStopAndClearBuffer,
    testStopAndDeletePlayer,
    testStopAndPlayAgain,
    testPauseAndResume,
    testImmediatePause,
    testExitLoop,
};

static void stressTestInit(StressTestApp *app) {
    void *androidActivity = NULL;
#if defined(MAL_EXAMPLE_WITH_GLFM) && defined(__ANDROID__)
    androidActivity = glfmAndroidGetActivity();
#endif
    app->context = malContextCreateWithOptions(MAL_DEFAULT_SAMPLE_RATE, androidActivity, NULL);
    if (!app->context) {
        printf("Error: Couldn't create audio context\n");
        app->state = STATE_FAIL;
        return;
    }

    // Create buffer (X seconds of silence)
    double duration = 10.0;
    double shortDuraton = 0.042; // 2.5 UI frames at 60Hz
    double mediumDuration = 0.75;
    app->format.sampleRate = malContextGetSampleRate(app->context);
    app->format.bitDepth = 16;
    app->format.numChannels = 1;
    app->bufferDataFrames = (uint32_t)(app->format.sampleRate * duration);
    uint32_t shortNumFrames = (uint32_t)(app->format.sampleRate * shortDuraton);
    uint32_t mediumNumFrames = (uint32_t)(app->format.sampleRate * mediumDuration);
    app->bufferData = calloc(app->bufferDataFrames, 2);
    if (!app->bufferData) {
        printf("Error: Couldn't create audio buffer data\n");
        app->state = STATE_FAIL;
        return;
    }
    app->buffer = malBufferCreate(app->context, app->format, app->bufferDataFrames, app->bufferData);
    if (!app->buffer) {
        printf("Error: Couldn't create audio buffer\n");
        app->state = STATE_FAIL;
        return;
    }
    app->shortBuffer = malBufferCreate(app->context, app->format, shortNumFrames, app->bufferData);
    if (!app->shortBuffer) {
        printf("Error: Couldn't create short audio buffer\n");
        app->state = STATE_FAIL;
        return;
    }
    app->mediumBuffer = malBufferCreate(app->context, app->format, mediumNumFrames, app->bufferData);
    if (!app->mediumBuffer) {
        printf("Error: Couldn't create medium audio buffer\n");
        app->state = STATE_FAIL;
        return;
    }

    app->startTime = time_us();
}

static void stressTestFree(StressTestApp *app) {
    if (app->program) {
        glDeleteProgram(app->program);
    }
    if (app->vertexBuffer) {
        glDeleteBuffers(1, &app->vertexBuffer);
    }
#if defined(MAL_EXAMPLE_WITH_GLFW)
    if (app->vertexArray) {
        glDeleteVertexArrays(1, &app->vertexArray);
    }
#endif
    malBufferRelease(app->buffer);
    malBufferRelease(app->shortBuffer);
    malBufferRelease(app->mediumBuffer);
    for (int i = 0; i < kNumPlayers; i++) {
        malBufferRelease(app->tempBuffers[i]);
    }
    for (int i = 0; i < kNumPlayers; i++) {
        malPlayerRelease(app->players[i]);
    }
    malContextRelease(app->context);
    free(app->bufferData);
    free(app);
}

static void doTestIterationAndDraw(StressTestApp *app) {
    malContextPollEvents(app->context);

    if (app->state != STATE_FAIL) {
        size_t numTests = sizeof(testFunctions) / sizeof(*testFunctions);
        TestFunction testFunction = testFunctions[app->currentTest];
        bool initialized = true;
        if (app->testIteration == 0) {
            initialized = createPlayersIfNeeded(app);
        }
        if (!initialized) {
            app->state = STATE_FAIL;
        } else {
            TestFunctionState functionState = testFunction(app);
            if (functionState.state == STATE_FAIL) {
                printf("Failure: %s iteration: %zu line: %zu\n", functionState.name,
                       app->testIteration, functionState.failureLine);
                if (functionState.failureReason[0] != 0) {
                    printf("Reason: %s\n", functionState.failureReason);
                }
                printPlayerStates(app);
                app->state = STATE_FAIL;
            } else if (functionState.state == STATE_SUCCESS) {
                if (app->currentTest == numTests - 1) {
                    app->state = STATE_SUCCESS;
                    app->successCount++;
                    app->currentTest = 0;
                    double duration = (time_us() - app->startTime) / 1000000.0;
                    printf("Successful runs: %zu Duration: %.3fs (%.3fs/run)\n", app->successCount,
                           duration, (duration /app->successCount));
                } else {
                    app->currentTest++;
                }
                app->testIteration = 0;
            } else {
                app->testIteration++;
            }
        }
    }

    float r, g, b;
    switch (app->state) {
        case STATE_TESTING: default:
            r = 0.6f; g = 0.6f; b = 0.6f;
            break;
        case STATE_SUCCESS:
            r = 0.2f; g = 0.8f; b = 0.1f;
            break;
        case STATE_FAIL:
            r = 0.8f; g = 0.1f; b = 0.1f;
            break;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw
    if (app->program == 0) {
        static const char *vertShaderSource =
            "#version 100\n"
            "attribute highp vec3 a_position;"
            "attribute lowp vec3 a_color;"
            "varying lowp vec3 v_color;"
            "void main() {"
            "    gl_Position = vec4(a_position, 1.0);"
            "    v_color = a_color;"
            "}";
        static const char *fragShaderSource =
            "#version 100\n"
            "varying lowp vec3 v_color;"
            "void main() {"
            "    gl_FragColor = vec4(v_color, 1.0);"
            "}";

        GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertShaderSource);
        GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragShaderSource);
        if (vertShader == 0 || fragShader == 0) {
            return;
        }
        app->program = glCreateProgram();

        glAttachShader(app->program, vertShader);
        glAttachShader(app->program, fragShader);

        glBindAttribLocation(app->program, 0, "a_position");
        glBindAttribLocation(app->program, 1, "a_color");

        glLinkProgram(app->program);

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
    }
    glUseProgram(app->program);

    if (app->vertexBuffer == 0) {
        glGenBuffers(1, &app->vertexBuffer);
    }
    glBindBuffer(GL_ARRAY_BUFFER, app->vertexBuffer);

#if defined(MAL_EXAMPLE_WITH_GLFW)
    if (app->vertexArray == 0) {
        glGenVertexArrays(1, &app->vertexArray);
    }
    glBindVertexArray(app->vertexArray);
#endif

    const size_t stride = sizeof(GLfloat) * 6;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(sizeof(GLfloat) * 3));

    float x = ((int64_t)(app->drawIteration % 100) - 50) / 50.0f;
    const GLfloat vertices[] = {
        // x,y,z, r,g,b
        x + 0.0f, 0.0f, 0.0f, r, g, b,
        x + 0.1f, 0.0f, 0.0f, r, g, b,
        x + 0.1f, 0.1f, 0.0f, r, g, b,
        x + 0.0f, 0.1f, 0.0f, r, g, b,
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (app->state != STATE_FAIL) {
        app->drawIteration++;
    }
}

#if defined(MAL_EXAMPLE_WITH_GLFM)

// MARK: GLFM functions

static void onFocusChange(GLFMDisplay *display, bool focused) {
    StressTestApp *app = glfmGetUserData(display);
    malContextSetActive(app->context, focused);
}

static void onSurfaceCreated(GLFMDisplay *display, int width, int height) {
    glViewport(0, 0, width, height);
}

static void onSurfaceDestroyed(GLFMDisplay *display) {
    StressTestApp *app = glfmGetUserData(display);
    app->program = 0;
    app->vertexBuffer = 0;
}

static void onFrame(GLFMDisplay *display, double frameTime) {
    StressTestApp *app = glfmGetUserData(display);
    doTestIterationAndDraw(app);
}

void glfmMain(GLFMDisplay *display) {
    StressTestApp *app = calloc(1, sizeof(StressTestApp));
    stressTestInit(app);

    glfmSetDisplayConfig(display,
                         GLFMRenderingAPIOpenGLES2,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMMultisampleNone);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, onSurfaceCreated);
    glfmSetSurfaceResizedFunc(display, onSurfaceCreated);
    glfmSetSurfaceDestroyedFunc(display, onSurfaceDestroyed);
    glfmSetAppFocusFunc(display, onFocusChange);
    glfmSetMainLoopFunc(display, onFrame);
}

#elif defined(MAL_EXAMPLE_WITH_GLFW)

// MARK: GLFW functions

static void onError(int error, const char *description) {
    printf("Error: %s\n", description);
}

int main(void) {
    GLFWwindow *window;
    glfwSetErrorCallback(onError);
    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(640, 480, "Mal Stress Test", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    StressTestApp *app = calloc(1, sizeof(StressTestApp));
    stressTestInit(app);
    glfwSetWindowUserPointer(window, app);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        doTestIterationAndDraw(app);

        int64_t startTime = time_us();
        glfwSwapBuffers(window);
        int64_t swapDur = time_us() - startTime;
        if (swapDur < 1000) {
            usleep(11000);
        }

        glfwPollEvents();
    }
    stressTestFree(app);
    glfwDestroyWindow(window);
    glfwTerminate();

    exit(EXIT_SUCCESS);
}

#endif
