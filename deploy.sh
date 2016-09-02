# This is not complete script, please refer to Gateway getting started guide to do complete steps
sudo service alljoyn-gwagent stop
sudo service alljoyn stop

export AJ_ROOT=/home/jay/alljoyn

# Build code
cd $AJ_ROOT/gateway/gwagent
scons BINDINGS=cpp OS=linux CPU=x86_64 VARIANT=debug POLICYDB=on ALLJOYN_DISTDIR=$AJ_ROOT/core/alljoyn/build/linux/x86_64/debug/dist WS=off BR=off

cd ~/alljoyn/staging

# make tar out of build
tar czf dummyApp1.tar.gz -C $AJ_ROOT/gateway/gwagent/build/linux/x86_64/debug/dist/gatewayConnector/tar .

# Remove exiting app
sudo rm -rf /opt/alljoyn/apps/dummyapp1

#Install App
sudo ./installPackage.sh dummyApp1.tar.gz

ls -la $AJ_ROOT/gateway/gwagent/build/linux/x86_64/debug/dist/gatewayConnector/tar

sudo cp -r /opt/alljoyn/apps /opt/alljoyn/alljoyn-daemon.d
sudo cp ~/all.acl /opt/alljoyn/apps/dummyapp1/acls/all

#Start Service
sudo service alljoyn start
sudo service alljoyn-gwagent start

