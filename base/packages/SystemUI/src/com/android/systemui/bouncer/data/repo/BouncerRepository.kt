/*
 * Copyright (C) 2023 The Android Open Source Project
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

package com.android.systemui.bouncer.data.repo

import com.android.systemui.bouncer.shared.model.AuthenticationThrottledModel
import com.android.systemui.dagger.SysUISingleton
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Provides access to bouncer-related application state. */
@SysUISingleton
class BouncerRepository @Inject constructor() {
    private val _message = MutableStateFlow<String?>(null)
    /** The user-facing message to show in the bouncer. */
    val message: StateFlow<String?> = _message.asStateFlow()

    private val _throttling = MutableStateFlow<AuthenticationThrottledModel?>(null)
    /** The current authentication throttling state. If `null`, there's no throttling. */
    val throttling: StateFlow<AuthenticationThrottledModel?> = _throttling.asStateFlow()

    fun setMessage(message: String?) {
        _message.value = message
    }

    fun setThrottling(throttling: AuthenticationThrottledModel?) {
        _throttling.value = throttling
    }
}