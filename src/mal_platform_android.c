/*
 Mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2018 David Brackeen

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if defined(__ANDROID__)

#define _GNU_SOURCE /* For pipe2 */

// MARK: JNI helpers

#include <jni.h>
#include <pthread.h>

#define _jniWasExceptionThrown(jniEnv) \
    ((*jniEnv)->ExceptionCheck(jniEnv) ? ((*jniEnv)->ExceptionClear(jniEnv), 1) : 0)

#define _jniClearException(jniEnv) do { \
    if ((*jniEnv)->ExceptionCheck(jniEnv)) { \
        (*jniEnv)->ExceptionClear(jniEnv); \
    } \
} while (0)

static jmethodID _jniGetMethodID(JNIEnv *jniEnv, jobject object, const char *name,
                                 const char *sig) {
    if (object) {
        jclass class = (*jniEnv)->GetObjectClass(jniEnv, object);
        jmethodID methodID = (*jniEnv)->GetMethodID(jniEnv, class, name, sig);
        (*jniEnv)->DeleteLocalRef(jniEnv, class);
        return _jniWasExceptionThrown(jniEnv) ? NULL : methodID;
    } else {
        return NULL;
    }
}

static jfieldID _jniGetFieldID(JNIEnv *jniEnv, jobject object, const char *name, const char *sig) {
    if (object) {
        jclass class = (*jniEnv)->GetObjectClass(jniEnv, object);
        jfieldID fieldID = (*jniEnv)->GetFieldID(jniEnv, class, name, sig);
        (*jniEnv)->DeleteLocalRef(jniEnv, class);
        return _jniWasExceptionThrown(jniEnv) ? NULL : fieldID;
    } else {
        return NULL;
    }
}

static jfieldID _jniGetStaticFieldID(JNIEnv *jniEnv, jclass class, const char *name,
                                     const char *sig) {
    if (class) {
        jfieldID fieldID = (*jniEnv)->GetStaticFieldID(jniEnv, class, name, sig);
        return _jniWasExceptionThrown(jniEnv) ? NULL : fieldID;
    } else {
        return NULL;
    }
}

#define _jniCallMethod(jniEnv, object, methodName, methodSig, returnType) \
    (*jniEnv)->Call##returnType##Method(jniEnv, object, \
        _jniGetMethodID(jniEnv, object, methodName, methodSig))

#define _jniCallMethodWithArgs(jniEnv, object, methodName, methodSig, returnType, ...) \
    (*jniEnv)->Call##returnType##Method(jniEnv, object, \
        _jniGetMethodID(jniEnv, object, methodName, methodSig), __VA_ARGS__)

#define _jniGetField(jniEnv, object, fieldName, fieldSig, fieldType) \
    (*jniEnv)->Get##fieldType##Field(jniEnv, object, \
        _jniGetFieldID(jniEnv, object, fieldName, fieldSig))

#define _jniGetStaticField(jniEnv, class, fieldName, fieldSig, fieldType) \
    (*jniEnv)->GetStatic##fieldType##Field(jniEnv, class, \
        _jniGetStaticFieldID(jniEnv, class, fieldName, fieldSig))

static pthread_key_t _malJNIEnvKey;
static pthread_once_t _malJNIEnvKeyOnce = PTHREAD_ONCE_INIT;

static void _malDetachJNIEnv(void *value) {
    if (value) {
        JavaVM *vm = (JavaVM *)value;
        (*vm)->DetachCurrentThread(vm);
    }
}

static void _malCreateJNIEnvKey() {
    pthread_key_create(&_malJNIEnvKey, _malDetachJNIEnv);
}

static JNIEnv *_malGetJNIEnv(JavaVM *vm) {
    JNIEnv *jniEnv = NULL;
    if ((*vm)->GetEnv(vm, (void **)&jniEnv, JNI_VERSION_1_4) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &jniEnv, NULL) == JNI_OK) {
            pthread_once(&_malJNIEnvKeyOnce, _malCreateJNIEnvKey);
            pthread_setspecific(_malJNIEnvKey, vm);
        }
    }
    return jniEnv;
}

// MARK: Mal platform implementation

#include "mal_audio_opensl.h"

void malContextPollEvents(MalContext *context) {
    (void)context;
    // Do nothing
}

static void _malContextGetSampleRate(MalContext *context) {
    if (!context->data.appContext || context->data.sdkVersion < 17) {
        return;
    }

    JNIEnv *jniEnv = _malGetJNIEnv(context->data.vm);
    if (!jniEnv) {
        return;
    }
    _jniClearException(jniEnv);

    if ((*jniEnv)->PushLocalFrame(jniEnv, 16) == JNI_OK) {
        #define MAL_JNI_CHECK(x) if (!(x) || (*jniEnv)->ExceptionCheck(jniEnv)) goto cleanup

        jclass contextClass = (*jniEnv)->FindClass(jniEnv, "android/content/Context");
        MAL_JNI_CHECK(contextClass);
        jstring audioServiceKey = _jniGetStaticField(jniEnv, contextClass, "AUDIO_SERVICE",
                                                     "Ljava/lang/String;", Object);
        MAL_JNI_CHECK(audioServiceKey);
        jobject audioManager = _jniCallMethodWithArgs(jniEnv, context->data.appContext,
                                                      "getSystemService",
                                                      "(Ljava/lang/String;)Ljava/lang/Object;",
                                                       Object, audioServiceKey);
        MAL_JNI_CHECK(audioManager);
        jclass audioManagerClass = (*jniEnv)->GetObjectClass(jniEnv, audioManager);
        MAL_JNI_CHECK(audioManagerClass);
        jstring sampleRateKey = _jniGetStaticField(jniEnv, audioManagerClass,
                                                   "PROPERTY_OUTPUT_SAMPLE_RATE",
                                                   "Ljava/lang/String;", Object);
        MAL_JNI_CHECK(sampleRateKey);
        jstring sampleRate = _jniCallMethodWithArgs(jniEnv, audioManager, "getProperty",
                                                    "(Ljava/lang/String;)Ljava/lang/String;",
                                                    Object, sampleRateKey);
        MAL_JNI_CHECK(sampleRate);
        const char *sampleRateString = (*jniEnv)->GetStringUTFChars(jniEnv, sampleRate, NULL);
        if (sampleRateString) {
            int sampleRateInt = atoi(sampleRateString);
            context->actualSampleRate = sampleRateInt > 0 ? sampleRateInt : 44100;
            (*jniEnv)->ReleaseStringUTFChars(jniEnv, sampleRate, sampleRateString);
        }
cleanup:
        #undef MAL_JNI_CHECK
        _jniClearException(jniEnv);
        (*jniEnv)->PopLocalFrame(jniEnv, NULL);
    }
}

static void _malContextDidCreate(MalContext *context) {
    _malContextGetSampleRate(context);
}

static void _malContextWillDispose(MalContext *context) {
    (void)context;
    // Do nothing
}

static void _malContextDidSetActive(MalContext *context, bool active) {
    (void)context;
    (void)active;
    // Do nothing
}

#endif
