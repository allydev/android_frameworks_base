/*
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
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

package com.android.server;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.BroadcastReceiver;
import android.os.RemoteException;
import android.os.ICheckinService;
import android.os.ServiceManager;
import android.net.Uri;
import android.provider.Settings;
import android.util.Log;
import java.util.StringTokenizer;

/**
 * Receives media mount broadcast masseges and launches the default file manager
 * to show contents of the external media ( USB Mass storage, SD Card).
 *
 */
public class FileManagerLauncher extends BroadcastReceiver {

    private static final String TAG = "FileManagerLauncher";
    private static final String activityName = "FileManagerActivity";

    @Override
    public void onReceive(Context context, Intent intent) {

        if (intent.getAction().equals(Intent.ACTION_MEDIA_MOUNTED)) {

           /**
            * Read the default file nanager name from the data base
            */
            String fileManagerPackage =
                     Settings.System.getString(context.getContentResolver(),
                                        Settings.System.DEFAULT_FILE_MANAGER);

            if (fileManagerPackage != null) {
                String fileManagerActivity = fileManagerPackage + "." +
                                                activityName;
               /**
                * Create a file manager Intent with the default file manager
                * and start the actvity
                */
                Intent fileManagerIntent = new Intent(Intent.ACTION_MAIN);
                fileManagerIntent.setClassName(fileManagerPackage,
                                                     fileManagerActivity);
                fileManagerIntent.setData(intent.getData());
                fileManagerIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

                try {
                    context.startActivity(fileManagerIntent);
                } catch (ActivityNotFoundException e) {
                    Log.w(TAG,"could not find file Manager Activity");
                }
            }
        }
    }
}
