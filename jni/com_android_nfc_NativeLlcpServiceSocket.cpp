/*
 * Copyright (C) 2010 The Android Open Source Project
 *
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

#include <semaphore.h>
#include <errno.h>

#include "com_android_nfc.h"

namespace android {

extern phLibNfc_Handle hIncommingLlcpSocket;
extern sem_t *nfc_jni_llcp_listen_sem;
extern void nfc_jni_llcp_transport_socket_err_callback(void*      pContext,
                                                       uint8_t    nErrCode);
/*
 * Callbacks
 */
static void nfc_jni_llcp_accept_socket_callback(void*        pContext,
                                                NFCSTATUS    status)
{
   struct nfc_jni_callback_data * pCallbackData = (struct nfc_jni_callback_data *) pContext;
   LOG_CALLBACK("nfc_jni_llcp_accept_socket_callback", status);

   /* Report the callback status and wake up the caller */
   pCallbackData->status = status;
   sem_post(&pCallbackData->sem);
}
 
 
/*
 * Methods
 */ 
static jobject com_NativeLlcpServiceSocket_doAccept(JNIEnv *e, jobject o, jint miu, jint rw, jint linearBufferLength)
{
   NFCSTATUS ret;
   struct timespec ts;
   phLibNfc_Llcp_sSocketOptions_t sOptions;
   phNfc_sData_t sWorkingBuffer;
   jfieldID f;   
   jclass clsNativeLlcpSocket;
   jobject clientSocket = NULL;
   struct nfc_jni_callback_data cb_data;

   /* Wait for tag Notification */
   if(sem_wait(nfc_jni_llcp_listen_sem) == -1)
   {
      return NULL;
   }
   
   /* Create the local semaphore */
   if (!nfc_cb_data_init(&cb_data, NULL))
   {
      goto clean_and_return;
   }

   /* Set socket options with the socket options of the service */
   sOptions.miu = miu;
   sOptions.rw = rw;
   
   /* Allocate Working buffer length */
   sWorkingBuffer.buffer = (uint8_t*)malloc((miu*rw)+miu+linearBufferLength);
   sWorkingBuffer.length = (miu*rw)+ miu + linearBufferLength;
   
   /* Accept the incomming socket */
   TRACE("phLibNfc_Llcp_Accept()");
   REENTRANCE_LOCK();
   ret = phLibNfc_Llcp_Accept( hIncommingLlcpSocket,
                               &sOptions,
                               &sWorkingBuffer,
                               nfc_jni_llcp_transport_socket_err_callback,
                               nfc_jni_llcp_accept_socket_callback,
                               (void*)&cb_data);
   REENTRANCE_UNLOCK();
   if(ret != NFCSTATUS_PENDING)
   {
      LOGE("phLibNfc_Llcp_Accept() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));
      return NULL;
   }
   TRACE("phLibNfc_Llcp_Accept() returned 0x%04x[%s]", ret, nfc_jni_get_status_name(ret));

   /* Wait for callback response */
   if(sem_wait(&cb_data.sem))
   {
      LOGE("Failed to wait for semaphore (errno=0x%08x)", errno);
      goto clean_and_return;
   }

   if(cb_data.status != NFCSTATUS_SUCCESS)
   {
      goto clean_and_return;
   }

   /* Create new LlcpSocket object */
   if(nfc_jni_cache_object(e,"com/android/nfc/NativeLlcpSocket",&(clientSocket)) == -1)
   {
      LOGD("LLCP Socket creation error");
      goto clean_and_return;
   }

   /* Get NativeConnectionOriented class object */
   clsNativeLlcpSocket = e->GetObjectClass(clientSocket);
   if(e->ExceptionCheck())
   {
      LOGD("LLCP Socket get class object error");
      goto clean_and_return;
   }

   /* Set socket handle */
   f = e->GetFieldID(clsNativeLlcpSocket, "mHandle", "I");
   e->SetIntField(clientSocket, f,(jint)hIncommingLlcpSocket);

   /* Set socket MIU */
   f = e->GetFieldID(clsNativeLlcpSocket, "mLocalMiu", "I");
   e->SetIntField(clientSocket, f,(jint)miu);

   /* Set socket RW */
   f = e->GetFieldID(clsNativeLlcpSocket, "mLocalRw", "I");
   e->SetIntField(clientSocket, f,(jint)rw);

   TRACE("socket handle 0x%02x: MIU = %d, RW = %d\n",hIncommingLlcpSocket, miu, rw);

clean_and_return:
   nfc_cb_data_deinit(&cb_data);
   return clientSocket;
}

static jboolean com_NativeLlcpServiceSocket_doClose(JNIEnv *e, jobject o)
{
   NFCSTATUS ret;
   phLibNfc_Handle hLlcpSocket;
   TRACE("Close Service socket");
   
   /* Retrieve socket handle */
   hLlcpSocket = nfc_jni_get_nfc_socket_handle(e,o);

   REENTRANCE_LOCK();
   ret = phLibNfc_Llcp_Close(hLlcpSocket);
   REENTRANCE_UNLOCK();
   if(ret == NFCSTATUS_SUCCESS)
   {
      TRACE("Close Service socket OK");
      return TRUE;
   }
   else
   {
      LOGD("Close Service socket KO");
      return FALSE;
   }
}


/*
 * JNI registration.
 */
static JNINativeMethod gMethods[] =
{
   {"doAccept", "(III)Lcom/android/nfc/NativeLlcpSocket;",
      (void *)com_NativeLlcpServiceSocket_doAccept},
      
   {"doClose", "()Z",
      (void *)com_NativeLlcpServiceSocket_doClose},
};


int register_com_android_nfc_NativeLlcpServiceSocket(JNIEnv *e)
{
   return jniRegisterNativeMethods(e,
      "com/android/nfc/NativeLlcpServiceSocket",
      gMethods, NELEM(gMethods));
}

} // namespace android
