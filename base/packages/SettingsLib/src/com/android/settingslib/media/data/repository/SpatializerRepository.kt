/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package com.android.settingslib.media.data.repository

import android.media.AudioDeviceAttributes
import android.media.Spatializer
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.withContext

interface SpatializerRepository {

    /**
     * Returns true when head tracking is available for the [audioDeviceAttributes] and false the
     * otherwise.
     */
    suspend fun isHeadTrackingAvailableForDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ): Boolean

    /**
     * Returns true when Spatial audio feature is supported for the [audioDeviceAttributes] and
     * false the otherwise.
     */
    suspend fun isSpatialAudioAvailableForDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ): Boolean

    /** Returns a list [AudioDeviceAttributes] that are compatible with spatial audio. */
    suspend fun getSpatialAudioCompatibleDevices(): Collection<AudioDeviceAttributes>

    /** Adds a [audioDeviceAttributes] to [getSpatialAudioCompatibleDevices] list. */
    suspend fun addSpatialAudioCompatibleDevice(audioDeviceAttributes: AudioDeviceAttributes)

    /** Removes a [audioDeviceAttributes] from [getSpatialAudioCompatibleDevices] list. */
    suspend fun removeSpatialAudioCompatibleDevice(audioDeviceAttributes: AudioDeviceAttributes)

    /** Checks if the head tracking is enabled for the [audioDeviceAttributes]. */
    suspend fun isHeadTrackingEnabled(audioDeviceAttributes: AudioDeviceAttributes): Boolean

    /** Sets head tracking [isEnabled] for the [audioDeviceAttributes]. */
    suspend fun setHeadTrackingEnabled(
        audioDeviceAttributes: AudioDeviceAttributes,
        isEnabled: Boolean,
    )
}

class SpatializerRepositoryImpl(
    private val spatializer: Spatializer,
    private val backgroundContext: CoroutineContext,
) : SpatializerRepository {

    override suspend fun isHeadTrackingAvailableForDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ): Boolean {
        return withContext(backgroundContext) { spatializer.hasHeadTracker(audioDeviceAttributes) }
    }

    override suspend fun isSpatialAudioAvailableForDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ): Boolean {
        return withContext(backgroundContext) {
            spatializer.isAvailableForDevice(audioDeviceAttributes)
        }
    }

    override suspend fun getSpatialAudioCompatibleDevices(): Collection<AudioDeviceAttributes> =
        withContext(backgroundContext) { spatializer.compatibleAudioDevices }

    override suspend fun addSpatialAudioCompatibleDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ) {
        withContext(backgroundContext) {
            spatializer.addCompatibleAudioDevice(audioDeviceAttributes)
        }
    }

    override suspend fun removeSpatialAudioCompatibleDevice(
        audioDeviceAttributes: AudioDeviceAttributes
    ) {
        withContext(backgroundContext) {
            spatializer.removeCompatibleAudioDevice(audioDeviceAttributes)
        }
    }

    override suspend fun isHeadTrackingEnabled(
        audioDeviceAttributes: AudioDeviceAttributes
    ): Boolean =
        withContext(backgroundContext) { spatializer.isHeadTrackerEnabled(audioDeviceAttributes) }

    override suspend fun setHeadTrackingEnabled(
        audioDeviceAttributes: AudioDeviceAttributes,
        isEnabled: Boolean,
    ) {
        withContext(backgroundContext) {
            spatializer.setHeadTrackerEnabled(isEnabled, audioDeviceAttributes)
        }
    }
}