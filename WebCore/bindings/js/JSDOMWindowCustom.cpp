/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "JSDOMWindowCustom.h"

#include "AtomicString.h"
#include "Base64.h"
#include "DOMWindow.h"
#include "Document.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameTree.h"
#include "JSDOMWindowShell.h"
#include "JSEventListener.h"
#include "JSMessagePort.h"
#include "MessagePort.h"
#include "ScriptController.h"
#include "Settings.h"
#include <kjs/JSObject.h>
#include <kjs/PrototypeFunction.h>

using namespace JSC;

namespace WebCore {

static void markDOMObjectWrapper(JSGlobalData& globalData, void* object)
{
    if (!object)
        return;
    DOMObject* wrapper = getCachedDOMObjectWrapper(globalData, object);
    if (!wrapper || wrapper->marked())
        return;
    wrapper->mark();
}

void JSDOMWindow::mark()
{
    JSGlobalData& globalData = *Heap::heap(this)->globalData();

    markDOMObjectWrapper(globalData, impl()->optionalConsole());
    markDOMObjectWrapper(globalData, impl()->optionalHistory());
    markDOMObjectWrapper(globalData, impl()->optionalLocationbar());
    markDOMObjectWrapper(globalData, impl()->optionalMenubar());
    markDOMObjectWrapper(globalData, impl()->optionalNavigator());
    markDOMObjectWrapper(globalData, impl()->optionalPersonalbar());
    markDOMObjectWrapper(globalData, impl()->optionalScreen());
    markDOMObjectWrapper(globalData, impl()->optionalScrollbars());
    markDOMObjectWrapper(globalData, impl()->optionalSelection());
    markDOMObjectWrapper(globalData, impl()->optionalStatusbar());
    markDOMObjectWrapper(globalData, impl()->optionalToolbar());
    markDOMObjectWrapper(globalData, impl()->optionalLocation());
#if ENABLE(DOM_STORAGE)
    markDOMObjectWrapper(globalData, impl()->optionalSessionStorage());
    markDOMObjectWrapper(globalData, impl()->optionalLocalStorage());
#endif
#if ENABLE(OFFLINE_WEB_APPLICATIONS)
    markDOMObjectWrapper(globalData, impl()->optionalApplicationCache());
#endif

    JSDOMStructureMap::iterator end = structures().end();
    for (JSDOMStructureMap::iterator it = structures().begin(); it != end; ++it)
        it->second->mark();

    JSDOMConstructorMap::iterator end2 = constructors().end();
    for (JSDOMConstructorMap::iterator it2 = constructors().begin(); it2 != end2; ++it2) {
        if (!it2->second->marked())
            it2->second->mark();
    }

    Base::mark();
}

bool JSDOMWindow::deleteProperty(ExecState* exec, const Identifier& propertyName)
{
    // Only allow deleting properties by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return false;
    return Base::deleteProperty(exec, propertyName);
}

bool JSDOMWindow::customGetPropertyNames(ExecState* exec, PropertyNameArray&)
{
    // Only allow the window to enumerated by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return true;
    return false;
}

bool JSDOMWindow::getPropertyAttributes(JSC::ExecState* exec, const Identifier& propertyName, unsigned& attributes) const
{
    // Only allow getting property attributes properties by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return false;
    return Base::getPropertyAttributes(exec, propertyName, attributes);
}

void JSDOMWindow::defineGetter(ExecState* exec, const Identifier& propertyName, JSObject* getterFunction)
{
    // Only allow defining getters by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return;
    Base::defineGetter(exec, propertyName, getterFunction);
}

void JSDOMWindow::defineSetter(ExecState* exec, const Identifier& propertyName, JSObject* setterFunction)
{
    // Only allow defining setters by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return;
    Base::defineSetter(exec, propertyName, setterFunction);
}

JSValue* JSDOMWindow::lookupGetter(ExecState* exec, const Identifier& propertyName)
{
    // Only allow looking-up getters by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return jsUndefined();
    return Base::lookupGetter(exec, propertyName);
}

JSValue* JSDOMWindow::lookupSetter(ExecState* exec, const Identifier& propertyName)
{
    // Only allow looking-up setters by frames in the same origin.
    if (!allowsAccessFrom(exec))
        return jsUndefined();
    return Base::lookupSetter(exec, propertyName);
}

void JSDOMWindow::setLocation(ExecState* exec, JSValue* value)
{
    Frame* activeFrame = asJSDOMWindow(exec->dynamicGlobalObject())->impl()->frame();
    if (!activeFrame)
        return;

#if ENABLE(DASHBOARD_SUPPORT)
    // To avoid breaking old widgets, make "var location =" in a top-level frame create
    // a property named "location" instead of performing a navigation (<rdar://problem/5688039>).
    if (Settings* settings = activeFrame->settings()) {
        if (settings->usesDashboardBackwardCompatibilityMode() && !activeFrame->tree()->parent()) {
            if (allowsAccessFrom(exec))
                putDirect(Identifier(exec, "location"), value);
            return;
        }
    }
#endif

    if (!activeFrame->loader()->shouldAllowNavigation(impl()->frame()))
        return;
    String dstUrl = activeFrame->loader()->completeURL(value->toString(exec)).string();
    if (!protocolIs(dstUrl, "javascript") || allowsAccessFrom(exec)) {
        bool userGesture = activeFrame->script()->processingUserGesture();
        // We want a new history item if this JS was called via a user gesture
        impl()->frame()->loader()->scheduleLocationChange(dstUrl, activeFrame->loader()->outgoingReferrer(), false, userGesture);
    }
}

JSValue* JSDOMWindow::postMessage(ExecState* exec, const ArgList& args)
{
    DOMWindow* window = impl();

    DOMWindow* source = asJSDOMWindow(exec->dynamicGlobalObject())->impl();
    String message = args.at(exec, 0)->toString(exec);

    if (exec->hadException())
        return jsUndefined();

    MessagePort* messagePort = (args.size() == 2) ? 0 : toMessagePort(args.at(exec, 1));

    String targetOrigin = valueToStringWithUndefinedOrNullCheck(exec, args.at(exec, (args.size() == 2) ? 1 : 2));
    if (exec->hadException())
        return jsUndefined();

    ExceptionCode ec = 0;
    window->postMessage(message, messagePort, targetOrigin, source, ec);
    setDOMException(exec, ec);

    return jsUndefined();
}

static JSValue* setTimeoutOrInterval(ExecState* exec, JSDOMWindow* window, const ArgList& args, bool timeout)
{
    JSValue* v = args.at(exec, 0);
    int delay = args.at(exec, 1)->toInt32(exec);
    if (v->isString())
        return jsNumber(exec, window->installTimeout(static_cast<JSString*>(v)->value(), delay, timeout));
    CallData callData;
    if (v->getCallData(callData) == CallTypeNone)
        return jsUndefined();
    ArgList argsTail;
    args.getSlice(2, argsTail);
    return jsNumber(exec, window->installTimeout(exec, v, argsTail, delay, timeout));
}

JSValue* JSDOMWindow::setTimeout(ExecState* exec, const ArgList& args)
{
    return setTimeoutOrInterval(exec, this, args, true);
}

JSValue* JSDOMWindow::clearTimeout(ExecState* exec, const ArgList& args)
{
    removeTimeout(args.at(exec, 0)->toInt32(exec));
    return jsUndefined();
}

JSValue* JSDOMWindow::setInterval(ExecState* exec, const ArgList& args)
{
    return setTimeoutOrInterval(exec, this, args, false);
}

JSValue* JSDOMWindow::clearInterval(ExecState* exec, const ArgList& args)
{
    removeTimeout(args.at(exec, 0)->toInt32(exec));
    return jsUndefined();
}

JSValue* JSDOMWindow::atob(ExecState* exec, const ArgList& args)
{
    if (args.size() < 1)
        return throwError(exec, SyntaxError, "Not enough arguments");

    JSValue* v = args.at(exec, 0);
    if (v->isNull())
        return jsEmptyString(exec);

    UString s = v->toString(exec);
    if (!s.is8Bit()) {
        setDOMException(exec, INVALID_CHARACTER_ERR);
        return jsUndefined();
    }

    Vector<char> in(s.size());
    for (int i = 0; i < s.size(); ++i)
        in[i] = static_cast<char>(s.data()[i]);
    Vector<char> out;

    if (!base64Decode(in, out))
        return throwError(exec, GeneralError, "Cannot decode base64");

    return jsString(exec, String(out.data(), out.size()));
}

JSValue* JSDOMWindow::btoa(ExecState* exec, const ArgList& args)
{
    if (args.size() < 1)
        return throwError(exec, SyntaxError, "Not enough arguments");

    JSValue* v = args.at(exec, 0);
    if (v->isNull())
        return jsEmptyString(exec);

    UString s = v->toString(exec);
    if (!s.is8Bit()) {
        setDOMException(exec, INVALID_CHARACTER_ERR);
        return jsUndefined();
    }

    Vector<char> in(s.size());
    for (int i = 0; i < s.size(); ++i)
        in[i] = static_cast<char>(s.data()[i]);
    Vector<char> out;

    base64Encode(in, out);

    return jsString(exec, String(out.data(), out.size()));
}

JSValue* JSDOMWindow::addEventListener(ExecState* exec, const ArgList& args)
{
    Frame* frame = impl()->frame();
    if (!frame)
        return jsUndefined();

    if (RefPtr<JSEventListener> listener = findOrCreateJSEventListener(exec, args.at(exec, 1))) {
        if (Document* doc = frame->document())
            doc->addWindowEventListener(AtomicString(args.at(exec, 0)->toString(exec)), listener.release(), args.at(exec, 2)->toBoolean(exec));
    }

    return jsUndefined();
}

JSValue* JSDOMWindow::removeEventListener(ExecState* exec, const ArgList& args)
{
    Frame* frame = impl()->frame();
    if (!frame)
        return jsUndefined();

    if (JSEventListener* listener = findJSEventListener(args.at(exec, 1))) {
        if (Document* doc = frame->document())
            doc->removeWindowEventListener(AtomicString(args.at(exec, 0)->toString(exec)), listener, args.at(exec, 2)->toBoolean(exec));
    }

    return jsUndefined();
}

DOMWindow* toDOMWindow(JSValue* val)
{
    if (val->isObject(&JSDOMWindow::s_info))
        return static_cast<JSDOMWindow*>(val)->impl();
    if (val->isObject(&JSDOMWindowShell::s_info))
        return static_cast<JSDOMWindowShell*>(val)->impl();
    return 0;
}

JSValue* nonCachingStaticCloseFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot& slot)
{
    return new (exec) PrototypeFunction(exec, 0, propertyName, jsDOMWindowPrototypeFunctionClose);
}

JSValue* nonCachingStaticBlurFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot& slot)
{
    return new (exec) PrototypeFunction(exec, 0, propertyName, jsDOMWindowPrototypeFunctionBlur);
}

JSValue* nonCachingStaticFocusFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot& slot)
{
    return new (exec) PrototypeFunction(exec, 0, propertyName, jsDOMWindowPrototypeFunctionFocus);
}

JSValue* nonCachingStaticPostMessageFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot& slot)
{
    return new (exec) PrototypeFunction(exec, 2, propertyName, jsDOMWindowPrototypeFunctionPostMessage);
}

} // namespace WebCore
