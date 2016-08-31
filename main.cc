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

#include <SrpKeyXListener.h>
#include <CommonSampleUtil.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/notification/Notification.h>
#include <alljoyn/notification/NotificationReceiver.h>
#include <alljoyn/notification/NotificationService.h>
#include "alljoyn/gateway/GatewayConnector.h"
#include <alljoyn/about/AboutServiceApi.h>
#include <alljoyn/PasswordManager.h>
#include <alljoyn/about/AnnounceHandler.h>
#include <alljoyn/about/AnnouncementRegistrar.h>
#include <alljoyn/config/ConfigClient.h>
#include <alljoyn/services_common/GuidUtil.h>

#include <iomanip>
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <set>

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

extern "C" {
    #include "MQTTClient.h"
    #include "MQTTClientPersistence.h"
    }

#include "DeviceController.h"

#define ADDRESS     "tcp://163.172.172.191:1883"
#define CLIENTID    "ExampleClientPub"
#define TOPIC       "heartbeat"
#define PAYLOAD     "Hello World HB!"
#define QOS         1
#define TIMEOUT     10000L

using namespace ajn;
using namespace qcc;
using namespace ajn::services;
using namespace ajn::gw;
using namespace std;

MQTTClient client;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

class ExitManager {
  public:
    ExitManager() : exiting(false), signum(0) { }
    ~ExitManager() { }

    bool isExiting() const
    {
        return exiting;
    }

    void setExiting(int32_t signum)
    {
        exiting = true;
        this->signum = signum;
    }

    int32_t getSignum() const
    {
        return signum;
    }

  private:
    bool exiting;
    int32_t signum;
};

ExitManager exitManager;


class ConfigSession : public BusAttachment::JoinSessionAsyncCB, public SessionListener {
  private:
    BusAttachment* bus;
    ConfigSession()
    {
        // Private to force use of the ctor with BusAttachment* parameter
    }
    void PrintAboutData(AboutClient::AboutData& aboutData)
    {
        for (AboutClient::AboutData::iterator itx = aboutData.begin(); itx != aboutData.end(); ++itx) {
            qcc::String key = itx->first;
            ajn::MsgArg value = itx->second;
            if (value.typeId == ALLJOYN_STRING) {
                std::cout << "Key name=" << key.c_str() << " value=" << value.v_string.str << std::endl;
            } else if (value.typeId == ALLJOYN_ARRAY && value.Signature().compare("as") == 0) {
                std::cout << "Key name=" << key.c_str() << " values: ";
                const MsgArg* stringArray;
                size_t fieldListNumElements;
                //QStatus status =
                value.Get("as", &fieldListNumElements, &stringArray);
                for (unsigned int i = 0; i < fieldListNumElements; i++) {
                    char* tempString;
                    stringArray[i].Get("s", &tempString);
                    std::cout << tempString << " ";
                }
                std::cout << std::endl;
            } else if (value.typeId == ALLJOYN_BYTE_ARRAY) {
                std::cout << "Key name=" << key.c_str() << " value=" << std::hex << std::uppercase << std::setfill('0');
                uint8_t* AppIdBuffer;
                size_t numElements;
                value.Get("ay", &numElements, &AppIdBuffer);
                for (size_t i = 0; i < numElements; i++) {
                    std::cout <<  std::setw(2) << (unsigned int)AppIdBuffer[i];
                }
                std::cout << std::nouppercase << std::dec << std::endl;
            }
        }
    }

  public:
    ConfigSession(BusAttachment* busAttachment) : bus(busAttachment) { }

    virtual void JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context) {
        static bool firstJoin = true;
        QStatus myStat;

        if (status != ER_OK) {
            cout << "Error joining session " <<  QCC_StatusText(status) << endl;
            free(context);
        } else {
            bus->EnableConcurrentCallbacks();
            if (firstJoin) {
                firstJoin = false;

                bool isIconInterface = false;
                bool isConfigInterface = false;
                AboutClient aboutClient(*bus);
                int ver = 0;

                AboutClient::ObjectDescriptions ObjectDescriptionsRefill;
                myStat = aboutClient.GetObjectDescriptions((char*)context, ObjectDescriptionsRefill, sessionId);

                if (myStat != ER_OK) {
                    cout << "getObjectDescriptions: status=" << QCC_StatusText(myStat) << endl;
                } else {
                    for (AboutClient::ObjectDescriptions::const_iterator it = ObjectDescriptionsRefill.begin();
                         it != ObjectDescriptionsRefill.end(); ++it) {
                        qcc::String key = it->first;
                        std::vector<qcc::String> vector = it->second;
                        cout << "key=" << key.c_str();
                        for (std::vector<qcc::String>::const_iterator itv = vector.begin(); itv != vector.end(); ++itv) {
                            if (key.compare("/About/DeviceIcon") == 0 && itv->compare("org.alljoyn.Icon") == 0) {
                                isIconInterface = true;
                            }
                            if (key.compare("/Config") == 0 && itv->compare("org.alljoyn.Config") == 0) {
                                isConfigInterface = true;
                            }
                            cout << " value=" << itv->c_str();
                        }
                        cout << endl;
                    }
                }

                AboutClient::AboutData aboutDataRefill;

                std::vector<qcc::String> supportedLanguages;
                myStat = aboutClient.GetAboutData((char*)context, NULL, aboutDataRefill);
                if (myStat != ER_OK) {
                    cout << "getAboutData: status="  << QCC_StatusText(myStat) << endl;
                } else {
                    AboutClient::AboutData::iterator search = aboutDataRefill.find("SupportedLanguages");
                    if (search != aboutDataRefill.end()) {
                        const MsgArg* stringArray;
                        size_t fieldListNumElements;
                        search->second.Get("as", &fieldListNumElements, &stringArray);
                        for (unsigned int i = 0; i < fieldListNumElements; i++) {
                            char* tempString;
                            stringArray[i].Get("s", &tempString);
                            supportedLanguages.push_back(tempString);
                        }
                    }
                }

                for (std::vector<qcc::String>::iterator it = supportedLanguages.begin(); it != supportedLanguages.end();
                     ++it) {
                    cout << endl << (char*)context << " AboutClient AboutData using language=" << it->c_str() << endl;
                    cout << "-----------------------------------" << endl;
                    AboutClient::AboutData aboutDataRefill;
                    myStat = aboutClient.GetAboutData((char*)context, it->c_str(), aboutDataRefill);
                    if (myStat != ER_OK) {
                        cout << "getAboutData: status="  << QCC_StatusText(myStat) << endl;
                    } else {
                        PrintAboutData(aboutDataRefill);
                    }
                }

                myStat = aboutClient.GetVersion((char*)context, ver, sessionId);
                if (myStat != ER_OK) {
                    cout << "getVersion: status=" << QCC_StatusText(myStat) << endl;
                } else {
                    cout << "Version=" << ver << endl;
                }

                if (isIconInterface) {
                    AboutIconClient iconClient(*bus);
                    qcc::String url;

                    myStat = iconClient.GetUrl((char*)context, url, sessionId);
                    if (myStat != ER_OK) {
                        cout << "getUrl: status= " << QCC_StatusText(myStat) << endl;
                    } else {
                        cout << "url=" << url.c_str() << endl;
                    }

                    AboutIconClient::Icon icon;
                    myStat = iconClient.GetIcon((char*)context, icon, sessionId);

                    if (myStat != ER_OK) {
                        cout << "GetIcon: status=" << QCC_StatusText(myStat) << endl;
                    } else {
                        cout << "Icon size=" << icon.contentSize << endl;
                        cout << "Icon mimetype=" << icon.mimetype << endl;
                        cout << "Icon content:\t";
                        for (size_t i = 0; i < icon.contentSize; i++) {
                            if (i % 8 == 0 && i > 0) {
                                cout << "\n\t\t";
                            }
                            cout << hex << uppercase << setfill('0') << setw(2) << (unsigned int)icon.content[i]
                                 << nouppercase << dec;
                        }
                        cout << endl;
                    }

                    myStat = iconClient.GetVersion((char*)context, ver, sessionId);
                    if (myStat != ER_OK) {
                        cout << "getVersion: status=" << QCC_StatusText(myStat) << endl;
                    } else {
                        cout << "Version=" << ver << endl;
                    }
                } // if (isIconInterface)

                if (isConfigInterface) {
                    ConfigClient configClient(*bus);

                    myStat = configClient.GetVersion((char*)context, ver, sessionId);
                    cout << "GetVersion: status=" << QCC_StatusText(myStat) << " version=" << ver << endl;

                    myStat = configClient.SetPasscode((char*)context, NULL, 6, (const uint8_t*)"000000", sessionId);
                    cout << "SetPasscode: status=" << QCC_StatusText(myStat) << endl;

                    ConfigClient::Configurations updateConfigurations;
                    updateConfigurations.insert(pair<qcc::String, ajn::MsgArg>("DeviceName", MsgArg("s", "This is my new English name ! ! ! !")));
                    myStat = configClient.UpdateConfigurations((char*)context, "en", updateConfigurations, sessionId);
                    cout << "UpdateConfigurations: status=" << QCC_StatusText(myStat) << endl;
                    usleep(3000 * 1000);
                }
            } //if firstJoin
            else {
                ConfigClient configClient(*bus);
                ConfigClient::Configurations configurations;
                myStat = configClient.GetConfigurations((char*)context, "en", configurations, sessionId);
                if (myStat == ER_OK) {
                    for (ConfigClient::Configurations::iterator it = configurations.begin();
                         it != configurations.end(); ++it) {
                        qcc::String key = it->first;
                        ajn::MsgArg value = it->second;
                        if (value.typeId == ALLJOYN_STRING) {
                            cout << "Key name=" << key.c_str() << " value=" << value.v_string.str << endl;
                        } else if (value.typeId == ALLJOYN_ARRAY && value.Signature().compare("as") == 0) {
                            cout << "Key name=" << key.c_str() << " values: ";
                            const MsgArg* stringArray;
                            size_t fieldListNumElements;
                            status = value.Get("as", &fieldListNumElements, &stringArray);
                            for (unsigned int i = 0; i < fieldListNumElements; i++) {
                                char* tempString;
                                stringArray[i].Get("s", &tempString);
                                cout << tempString << " ";
                            }
                            cout << endl;
                        }
                    }
                } else {
                    cout << "GetConfigurations: status=" << QCC_StatusText(myStat) << endl;
                }
            }
            free(context);
            bus->LeaveSession(sessionId);
            delete this;
        }
    }

};

class ConfigAboutListener : public AboutListener {
  private:
    BusAttachment* bus;
    ConfigAboutListener()
    {
        // Private to force use of ctor with BusAttachment* parameter
    }
  public:
    ConfigAboutListener(BusAttachment* busAttachment) : bus(busAttachment) { }

    virtual void Announced(const char* busName, uint16_t version, SessionPort port,
                           const MsgArg& objectDescriptionArg, const MsgArg& aboutDataArg) {

        QStatus status = ER_OK;

        cout << "Received Announce from " << busName << endl;

        // Go through the object descriptions to find the Config interface
        MsgArg*entries;
        typedef struct {
            char* objectPath;
            MsgArg* interfaces;
            size_t numInterfaces;
        } ObjectDescription;
        size_t num = 0;
        bool found = false;
        status = objectDescriptionArg.Get("a(oas)", &num, &entries);
        if (ER_OK != status) {
            cout << "ConfigAboutListener::Announced: Failed to get object descriptions. Status="
                 << QCC_StatusText(status) << endl;
            return;
        }
        for (size_t i = 0; i > num && !found; ++i) {
            ObjectDescription objDesc;
            status = entries[i].Get("(oas)", &objDesc.objectPath, &objDesc.interfaces, &objDesc.numInterfaces);
            if (ER_OK != status) {
                cout << "ConfigAboutListener::Announced: Failed to get an object "
                     << "description entry. Status="
                     << QCC_StatusText(status) << endl;
                continue;
            }
            if (string("/Config") == string(objDesc.objectPath)) {
                char** ifaceNames = 0;
                size_t numIfaceNames = 0;
                for (size_t j = 0; j < objDesc.numInterfaces && !found; ++j) {
                    status = objDesc.interfaces[j].Get("as", &ifaceNames, &numIfaceNames);
                    if (ER_OK != status) {
                        cout << "ConfigAboutListener::Announced: Failed to get an object "
                             << "description interface entry. Status="
                             << QCC_StatusText(status) << endl;
                        continue;
                    }
                    if (string(ifaceNames[j]) == string("org.alljoyn.Config")) {
                        // We found the Config interface so continue below
                        found = true;
                    }
                }
            }
        }
        if (!found) { return; }

        SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
        ConfigSession* cs = new ConfigSession(bus);
        bus->JoinSessionAsync(busName, port, cs, opts, cs, strdup(busName));
    }

};

void signal_callback_handler(int32_t signum) {
    exitManager.setExiting(signum);
}

void dumpObjectSpecs(list<GatewayMergedAcl::ObjectDescription>& specs, const char* indent) {
    list<GatewayMergedAcl::ObjectDescription>::iterator it;
    for (it = specs.begin(); it != specs.end(); it++) {
        GatewayMergedAcl::ObjectDescription& spec = *it;
        cout << indent << "objectPath: " << spec.objectPath.c_str() << endl;
        cout << indent << "isPrefix: " << (spec.isPrefix ? "true" : "false") << endl;

        list<qcc::String>::iterator innerator;
        for (innerator = spec.interfaces.begin(); innerator != spec.interfaces.end(); innerator++) {
            cout << indent << "    " << "interface: " << (*innerator).c_str() << endl;
        }
    }
}

void dumpAcl(GatewayMergedAcl* p) {
    cout << "Exposed Services:" << endl;
    dumpObjectSpecs(p->m_ExposedServices, "");
    cout << endl;


    cout << "Remoted Apps:" << endl;
    list<GatewayMergedAcl::RemotedApp>::iterator it;
    for (it = p->m_RemotedApps.begin(); it != p->m_RemotedApps.end(); it++) {
        GatewayMergedAcl::RemotedApp& rapp = *it;
        cout << rapp.deviceId.c_str() << " ";
        for (int i = 0; i < 16; i++) cout << hex << (unsigned int)rapp.appId[i];
        cout << endl;
        cout << "    Object Specs:" << endl;
        dumpObjectSpecs(rapp.objectDescs, "    ");
    }
}

class MyApp : public GatewayConnector {
  public:
    MyApp(BusAttachment* bus, qcc::String wkn) : GatewayConnector(bus, wkn) { }

  protected:
    virtual void mergedAclUpdated() {
        cout << "Merged Acl updated" << endl;
        GatewayMergedAcl* mergedAcl = new GatewayMergedAcl();
        QStatus status = getMergedAclAsync(mergedAcl);
        if (ER_OK != status) { delete mergedAcl; }
    }
    virtual void shutdown() {
        cout << "shutdown" << endl;
        kill(getpid(), SIGINT);
    }
    virtual void receiveGetMergedAclAsync(QStatus unmarshalStatus, GatewayMergedAcl* response) {
        if (ER_OK != unmarshalStatus) {
            cout << "Profile failed to unmarshal " << unmarshalStatus << endl;
        } else {
            dumpAcl(response);
        }

        delete response;
    }
};

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    printf("Message with token value %d delivery confirmed\n", dt);
    cout << "Message with token value " << dt << endl;
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    int i;
    char* payloadptr;

    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: ");

    payloadptr = (char*)message->payload;
    for(i=0; i<message->payloadlen; i++)
    {
        putchar(*payloadptr++);
    }
    putchar('\n');
    // Now Trigger Method
    ActionInfo actionInfo("uniqueName", "/ControlPanel/MyDevice/rootContainer/en/controlsContainer/ac_mode", "org.alljoyn.ControlPanel.Property", "setValue", "q", "749df3c84b2e489cbd534cc8fd3fd5f4", "749df3c84b2e489cbd534cc8fd3fd5f4");
    cout << "Message got due to subscribe: " << (char*)payloadptr << "  Message: " << *payloadptr << endl;
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
    printf("\nConnection lost\n");
    printf("     cause: %s\n", cause);
    // MQTT client connection 
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
	{
          printf("Failed to connect, return code %d\n", rc);
          //exit(EXIT_FAILURE);
	}
    // Subscribe to topic
    MQTTClient_subscribe(client, TOPIC, QOS);
}

class MyReceiver : public NotificationReceiver {
  private:
    qcc::String tweetScript;
    MyReceiver()
    {
        // Private to force use of other ctor
    }
  public:
    MyReceiver(const qcc::String& tweetScriptStr) : tweetScript(tweetScriptStr)
    {
    }
    virtual void Receive(Notification const& notification) {
        vector<NotificationText> vecMessages = notification.getText();

	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	MQTTClient_deliveryToken token;
	pubmsg.qos = QOS;
	pubmsg.retained = 0;

        for (vector<NotificationText>::const_iterator it = vecMessages.begin(); it != vecMessages.end(); ++it) {
            cout << "Notification to publish CIoT is: " << it->getLanguage().c_str() << endl;

            if (it->getLanguage().compare("en") == 0) {
		pubmsg.payload = (char*)it->getText().c_str();
	    	pubmsg.payloadlen = strlen(it->getText().c_str());
                MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    		cout << "MQTT msg pubslished";
            }
        }

	//while(deliveredtoken != token);
    	//MQTTClient_disconnect(client, 10000);
    	//MQTTClient_destroy(&client);
	//cout << "MQTT client disconnected:";
    }

    virtual void Dismiss(const int32_t msgId, const qcc::String appId) {
        cout << "Received notification dismiss for msg=" << msgId << " from app=" << appId.c_str() << endl;
    }
};


int main(int argc, char** argv) {
    // MQTT client connection 
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
	{
          printf("Failed to connect, return code %d\n", rc);
          //exit(EXIT_FAILURE);
	}
    // Subscribe to topic
    MQTTClient_subscribe(client, TOPIC, QOS);
    // End of MQTT
	
    signal(SIGINT, signal_callback_handler);
    BusAttachment bus("ConnectorApp", true);
    CommonBusListener busListener;
    SrpKeyXListener keyListener;

    //====================================
    // Initialize bus
    //====================================
#ifdef QCC_USING_BD
    PasswordManager::SetCredentials("ALLJOYN_PIN_KEYX", "000000");
#endif

    QStatus status = bus.Start();
    if (ER_OK != status) {
        cout << "Error starting bus: " << QCC_StatusText(status) << endl;
        return 1;
    }

    status = bus.Connect();
    if (ER_OK != status) {
        cout << "Error connecting bus: " << QCC_StatusText(status) << endl;
        return 1;
    }

    char* wkn = getenv("WELL_KNOWN_NAME");
    qcc::String wellknownName = wkn ? wkn : "dummyapp1";

    char* interOff = getenv("INTERACTIVE_OFF");
    bool notInteractive = (interOff && (strcmp(interOff, "1") == 0)) ? true : false;

    char* twScript = getenv("TWITTER_SCRIPT");
    qcc::String tweetScript = twScript ? "/opt/alljoyn/apps/" + wellknownName + "/bin/" +  twScript : "";

    //====================================
    // Initialize authentication
    //====================================
    keyListener.setPassCode("000000");
    qcc::String keystore = "/opt/alljoyn/apps/" + wellknownName + "/store/.alljoyn_keystore.ks";
    status = bus.EnablePeerSecurity("ALLJOYN_PIN_KEYX ALLJOYN_SRP_KEYX ALLJOYN_ECDHE_PSK", &keyListener, keystore.c_str(), false);

    //====================================
    // Initialize GwConnector interface
    //====================================
    MyApp myApp(&bus, wellknownName.c_str());
    status = myApp.init();
    if (ER_OK != status) {
        cout << "Error connecting bus: " << QCC_StatusText(status) << endl;
        return 1;
    }

    //====================================
    // Initialize notification consumer
    //====================================
    NotificationService* notificationService = NotificationService::getInstance();
    MyReceiver receiver(tweetScript);
    status = notificationService->initReceive(&bus, &receiver);
    if (ER_OK != status) {
        cout << "Error initializing notification receiver: " << QCC_StatusText(status) << endl;
        notificationService->shutdown();
        return 1;
    }


    //====================================
    // Initialize notification producer
    //====================================
    qcc::String deviceid;
    GuidUtil::GetInstance()->GetDeviceIdString(&deviceid);
    qcc::String appid;
    GuidUtil::GetInstance()->GenerateGUID(&appid);

    AboutData aboutData("en");
    AboutObj aboutObj(bus);
    DeviceNamesType deviceNames;
    deviceNames.insert(pair<qcc::String, qcc::String>("en", "ConnectorSampleDevice"));
    status = CommonSampleUtil::fillAboutData(&aboutData, appid, "ConnectorSample", deviceid, deviceNames);
    if (status != ER_OK) {
        cout << "Could not fill AboutData. " <<  QCC_StatusText(status) << endl;
        return 1;
    }
    status = CommonSampleUtil::prepareAboutService(&bus, &aboutData, &aboutObj, &busListener, 900);
    if (status != ER_OK) {
        cout << "Could not set up the AboutService." << endl;
        notificationService->shutdown();
        return 1;
    }
    NotificationSender* notificationSender = notificationService->initSend(&bus, &aboutData);
    if (!notificationSender) {
        cout << "Could not initialize Sender" << endl;
        notificationService->shutdown();
        return 1;
    }


    //====================================
    // Register for config announcements
    //====================================
    ConfigAboutListener aboutListener(&bus);
    bus.RegisterAboutListener(aboutListener);

    // Register for all Announcement and store busname and freindly name in map
    
    DeviceController deviceController(&bus);
    deviceController.initialize(argv[2]);


    //====================================
    // Here we go
    //====================================
    size_t lineSize = 1024;
    char line[1024];
    char* buffy = line;
    while (!exitManager.isExiting()) {
        if (notInteractive) {
            sleep(5);
            continue;
        }
        putchar('>');
        if (-1 == getline(&buffy, &lineSize, stdin)) {
            break;
        }
        char* cmd = strtok(buffy, " \r\n\t");
        if (NULL == cmd) {
            continue;
        }
        cout << "Got command " << cmd << endl;

        if (0 == strcmp(cmd, "GetMergedAcl")) {
            GatewayMergedAcl macl;
            QStatus status = myApp.getMergedAcl(&macl);
            cout << "GetMergedAcl returned " << status << endl;
            if (status == ER_OK) {
                dumpAcl(&macl);
            }
        } else if (0 == strcmp(cmd, "UpdateConnectionStatus")) {
            char* s = strtok(NULL, " \r\t\n");
            if (NULL == s) {
                cout << "Please try again and specify the new connection status" << endl;
                continue;
            }
            int i = atoi(s);
            myApp.updateConnectionStatus((ConnectionStatus)i);
        } else if (0 == strcmp(cmd, "Notify")) {
            char* typeStr = strtok(NULL, " \r\t\n");
            if (NULL == typeStr) {
                cout << "Something went wrong sending message" << endl;
                continue;
            }
            char* msg = typeStr + strlen(typeStr) + 1;

            vector<NotificationText> msgs;
            msgs.push_back(NotificationText("en", msg));

            status = notificationSender->send(Notification((NotificationMessageType)atoi(typeStr), msgs), 7200);
            cout << "send returned " << QCC_StatusText(status) << endl;
        } else if (0 == strcmp(cmd, "Exit")) {
            break;
        } else {
            cout << "Type one of:" << endl
                 << "GetMergedAcl<CR>" << endl
                 << "UpdateConnectionStatus 0|1|2|3|4<CR>" << endl
                 << "Notify 0|1|2 the rest of the message<CR>" << endl
                 << "Exit" << endl;
        }
    }

    notificationService->shutdownSender();
    notificationService->shutdown();
    return exitManager.getSignum();
}
