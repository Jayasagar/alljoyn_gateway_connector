/******************************************************************************
 * Copyright (c) 2014, AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include "DeviceController.h"

using namespace std;
using namespace ajn;
using namespace qcc;
using namespace services;

void DeviceController::initialize(const char* packageName) {
    QStatus status = ER_OK;

        AnnouncementRegistrar::RegisterAnnounceHandler(*mBusAttachment, *this, NULL, 0);

        /* Add the match so we receive sessionless signals */
        status = mBusAttachment->AddMatch("sessionless='t'");

        if (ER_OK != status) {
            cout << "Failed to addMatch for sessionless signals:" << QCC_StatusText(status) << endl;
        }
}

void DeviceController::Announce(unsigned short version, unsigned short port, const char* busName,
                           const ObjectDescriptions& objectDescs,
                           const AboutData& aboutData)
{
    cout << "Found about application with busName=" << busName << " port=" << port << endl;
    /* For now lets just assume everything has events and actions and join */
    char* friendlyName = NULL;
    for (AboutClient::AboutData::const_iterator it = aboutData.begin(); it != aboutData.end(); ++it) {
        qcc::String key = it->first;
        ajn::MsgArg value = it->second;
        if (value.typeId == ALLJOYN_STRING) {
            if (key.compare("DeviceName") == 0) {
                friendlyName = ::strdup(value.v_string.str);
                mBusFriendlyMap.insert(std::pair<qcc::String, qcc::String>(busName, friendlyName));
                mBusPortMap.insert(std::pair<qcc::String, short>(busName, port));
                //cout <<"Friendly Name: %s (%s)", friendlyName, busName);
                free(friendlyName);
            }
            //cout <<"(Announce handler) aboutData (key, val) (%s, %s)", key.c_str(), value.v_string.str);
        }
    }

    joinSession(busName, port);
}

void DeviceController::joinSession(const char* sessionName, short port)
{
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
    QStatus status = mBusAttachment->JoinSessionAsync(sessionName, port, this, opts, this, ::strdup(sessionName));
    cout << "JoinSessionAsync status:" << status << endl;
}

void DeviceController::JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context) {
    if (status == ER_OK || status == ER_ALLJOYN_JOINSESSION_REPLY_ALREADY_JOINED) {
        //cast context to a char* then check sessionName to save off correct sessionId value
        char* sessionName = (char*)context;
        cout << "Joined the sesssion have sessionId: " << sessionName << " Id: " <<sessionId <<endl;
        mBusSessionMap.insert(std::pair<qcc::String, int>(sessionName, sessionId));
    } else {
        cout << "Error joining status :" << endl;
    }
}

void DeviceController::InformFound(char* sessionName, int sessionId, char* friendly)
{
    
}

char* DeviceController::introspectWithDescriptions(const char* sessionName, const char* path, int sessionId) {
    cout << "introspectWithDescriptions the sesssion  path id :" << sessionName << path << sessionId << endl;
    ProxyBusObject remoteObj(*mBusAttachment, sessionName, path, sessionId);
    QStatus status = ER_OK;

    const char* ifcName = org::allseen::Introspectable::InterfaceName;

    const InterfaceDescription* introIntf = remoteObj.GetInterface(ifcName);
    if (!introIntf) {
        introIntf = mBusAttachment->GetInterface(ifcName);
        assert(introIntf);
        remoteObj.AddInterface(*introIntf);
    }

    /* Attempt to retrieve introspection from the remote object using sync call */
    Message reply(*mBusAttachment);
    const InterfaceDescription::Member* introMember = introIntf->GetMember("IntrospectWithDescription");
    assert(introMember);

    MsgArg inputs[1];
    inputs[0].Set("s", "en");
    status = remoteObj.MethodCall(*introMember, inputs, 1, reply, 30000);

    /* Parse the XML reply */
    if (status != ER_OK) {
        cout << "Introspection error:" << status << endl;
        mBusAttachment->LeaveSession(sessionId);
        return NULL;
    }

    //Go ahead and tell AllJoyn to set the interfaces now and save us an Introspection request later
    remoteObj.ParseXml(reply->GetArg(0)->v_string.str);

    return ::strdup(reply->GetArg(0)->v_string.str);
}

void DeviceController::callAction(ActionInfo* action)
{
    ajn::ProxyBusObject* actionObject;
    ajn::SessionId mSessionId;
    QStatus status = ER_OK;
    short port = mBusPortMap[action->mUniqueName];

    cout << "callAction on, port " << action->mUniqueName.c_str() << port << endl;
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
    status = mBusAttachment->JoinSession(action->mUniqueName.c_str(), port, this,  mSessionId, opts);
    if ((ER_OK == status || ER_ALLJOYN_JOINSESSION_REPLY_ALREADY_JOINED == status)) {
        //cout <<"Creating ProxyBusObject with SessionId: %d", mSessionId);
        actionObject = new ProxyBusObject(*mBusAttachment, action->mUniqueName.c_str(), action->mPath.c_str(), mSessionId);
        //actionObject = new ProxyBusObject(*mBusAttachment, action->mUniqueName.c_str(), action->mPath.c_str(), 0);
        const InterfaceDescription* actionIntf = mBusAttachment->GetInterface(action->mIfaceName.c_str());
        if (actionIntf) {
            actionObject->AddInterface(*actionIntf);
        } else {
            status = actionObject->IntrospectRemoteObject();
            cout << "Introspect Object called " << QCC_StatusText(status)<< status << endl;
        }
        actionIntf = mBusAttachment->GetInterface(action->mIfaceName.c_str());
        if (actionIntf && actionObject) {
            cout <<"Calling device() action "<<
                    action->mUniqueName.c_str()<< action->mIfaceName.c_str()<<
                    action->mMember.c_str()<< action->mSignature.c_str() << endl;
            //status = actionObject->MethodCall(action->mIfaceName.c_str(), action->mMember.c_str(), NULL, 0, );
            Message reply(*mBusAttachment);
            const InterfaceDescription::Member* methodMember = actionIntf->GetMember(action->mMember.c_str());
            status = actionObject->MethodCall(*methodMember, NULL, 0, reply);
            //cout <<"MethodCall status: %s(%x)", QCC_StatusText(status), status);
        } else {
            cout <<"Failed MethodCall status: %s(%x)" << QCC_StatusText(status) << status << endl;
        }
        mBusAttachment->LeaveSession(mSessionId);
    } else {
        //cout <<"Failed to join session status: %s(%x)", QCC_StatusText(status), status);
    }
}

bool DeviceController::enableEvent(EventInfo* event)
{
    QStatus status = ER_OK;
    const ajn::InterfaceDescription::Member* eventMember = NULL;
    bool ret = false;

    qcc::String matchRule = "type='signal',interface='";
    matchRule.append(event->mIfaceName);
    matchRule.append("',member='");
    matchRule.append(event->mMember);
    matchRule.append("'");

    cout <<"Going to setup a handler for the event:" <<
            matchRule.c_str() << endl;

    const InterfaceDescription* existingIntf = mBusAttachment->GetInterface(event->mIfaceName.c_str());
    if (existingIntf) {
        eventMember = existingIntf->GetMember(event->mMember.c_str());
    }

    if (eventMember) {
        status =  mBusAttachment->RegisterSignalHandler(this,
                                                        static_cast<MessageReceiver::SignalHandler>(&DeviceController::EventHandler),
                                                        eventMember,
                                                        NULL);

        if (status == ER_OK) {
            status = mBusAttachment->AddMatch(matchRule.c_str());
            ret = true;
        } else {
            cout <<"Error registering the signal handler: " << QCC_StatusText(status)<< status<< endl;
        }
    } else {
        cout <<"Event Member is null, get interface status "<< QCC_StatusText(status) <<status << endl;
    }

    return ret;
}

void DeviceController::EventHandler(const ajn::InterfaceDescription::Member* member, const char* srcPath, ajn::Message& msg)
{
    cout << "Event handler called" << endl;
}
//onEventReceived(String from, String path,
//String iface, String member, String sig)

void DeviceController::leaveSession(int sessionId)
{
    QStatus status = mBusAttachment->LeaveSession(sessionId);
    if (ER_OK != status) {
        cout <<"LeaveSession failed" << endl;
    } else {
        cout <<"LeaveSession successful" << endl;
    }
}

void DeviceController::shutdown()
{
    /* Cancel the advertisement */
    /* Unregister the Bus Listener */
    mBusAttachment->UnregisterBusListener(*((BusListener*)this));
    /* Deallocate the BusAttachment */
    if (mBusAttachment) {
        delete mBusAttachment;
        mBusAttachment = NULL;
    }
}

void DeviceController::AsyncCallReplyHandler(Message& msg, void* context) {
    size_t numArgs;
    const MsgArg* args;
    msg->GetArgs(numArgs, args);
    if (MESSAGE_METHOD_RET != msg->GetType()) {
        cout <<"Failed MethodCall message return type:"<< msg->GetType() << endl;
        cout <<"Failed MethodCall message Error name: "<< msg->GetErrorDescription().c_str() << endl;
    } else {
        cout <<"Action should have been executed" << endl ;
    }
}

/* From SessionListener */
void DeviceController::SessionLost(ajn::SessionId sessionId)
{
}




