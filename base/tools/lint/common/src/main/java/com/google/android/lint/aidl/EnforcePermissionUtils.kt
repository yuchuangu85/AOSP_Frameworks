/*
 * Copyright (C) 2022 The Android Open Source Project
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

package com.google.android.lint.aidl

import com.android.tools.lint.detector.api.JavaContext
import com.android.tools.lint.detector.api.LintFix
import com.android.tools.lint.detector.api.Location
import com.intellij.psi.PsiClass
import com.intellij.psi.PsiReferenceList
import org.jetbrains.uast.UMethod

/**
 * Given a UMethod, determine if this method is the entrypoint to an interface
 * generated by AIDL, returning the interface name if so, otherwise returning
 * null
 */
fun getContainingAidlInterface(context: JavaContext, node: UMethod): String? {
    val containingStub = containingStub(context, node) ?: return null
    val superMethod = node.findSuperMethods(containingStub)
    if (superMethod.isEmpty()) return null
    return containingStub.containingClass?.name
}

/* Returns the containing Stub class if any. This is not sufficient to infer
 * that the method itself extends an AIDL generated method. See
 * getContainingAidlInterface for that purpose.
 */
fun containingStub(context: JavaContext, node: UMethod?): PsiClass? {
    var superClass = node?.containingClass?.superClass
    while (superClass != null) {
        if (isStub(context, superClass)) return superClass
        superClass = superClass.superClass
    }
    return null
}

private fun isStub(context: JavaContext, psiClass: PsiClass?): Boolean {
    if (psiClass == null) return false
    if (psiClass.name != "Stub") return false
    if (!context.evaluator.isStatic(psiClass)) return false
    if (!context.evaluator.isAbstract(psiClass)) return false

    if (!hasSingleAncestor(psiClass.extendsList, BINDER_CLASS)) return false

    val parent = psiClass.parent as? PsiClass ?: return false
    if (!hasSingleAncestor(parent.extendsList, IINTERFACE_INTERFACE)) return false

    val parentName = parent.qualifiedName ?: return false
    if (!hasSingleAncestor(psiClass.implementsList, parentName)) return false

    return true
}

private fun hasSingleAncestor(references: PsiReferenceList?, qualifiedName: String) =
        references != null &&
                references.referenceElements.size == 1 &&
                references.referenceElements[0].qualifiedName == qualifiedName

fun getHelperMethodCallSourceString(node: UMethod) = "${node.name}$AIDL_PERMISSION_HELPER_SUFFIX()"

fun getHelperMethodFix(
    node: UMethod,
    manualCheckLocation: Location,
    prepend: Boolean = true
): LintFix {
    val helperMethodSource = getHelperMethodCallSourceString(node)
    val indent = " ".repeat(manualCheckLocation.start?.column ?: 0)
    val newText = "$helperMethodSource;${if (prepend) "\n\n$indent" else ""}"

    val fix = LintFix.create()
            .replace()
            .range(manualCheckLocation)
            .with(newText)
            .reformat(true)
            .autoFix()

    if (prepend) fix.beginning()

    return fix.build()
}