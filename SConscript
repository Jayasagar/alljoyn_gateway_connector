# Copyright AllSeen Alliance. All rights reserved.
#
#    Permission to use, copy, modify, and/or distribute this software for any
#    purpose with or without fee is hereby granted, provided that the above
#    copyright notice and this permission notice appear in all copies.
#
#    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

Import('gateway_env')

gwcnc_env = gateway_env.Clone()
gwcnc_env.Append(CPPPATH = gwcnc_env.Dir('$APP_COMMON_DIR/cpp/samples_common').srcnode())
gwcnc_env.Prepend(LIBS = ['paho-mqtt3a', 'paho-mqtt3c', 'alljoyn_about', 'alljoyn_services_common', 'alljoyn_notification', 'alljoyn_config', 'alljoyn_gwconnector'])

srcs = gwcnc_env.Glob('*.cc')
objs = gwcnc_env.Object(srcs)

gwcnc_env.VariantDir('AppCommon', '$APP_COMMON_DIR/cpp/samples_common/', duplicate = 0)
cobjs = gwcnc_env.SConscript('AppCommon/SConscript', {'env': gwcnc_env})
objs.extend(cobjs)

prog = gwcnc_env.Program('alljoyn-gwconnectorsample', objs)

# Prepare for building tar
gwcnc_env.Install('$GWCNC_DISTDIR/tar', gwcnc_env.Glob('Manifest.xml'))
gwcnc_env.Install('$GWCNC_DISTDIR/tar/bin', prog)
gwcnc_env.Install('$GWCNC_DISTDIR/tar/bin', gwcnc_env.Glob('postTweet.sh'))
gwcnc_env.Install('$GWCNC_DISTDIR/tar/bin', gwcnc_env.Glob('directMessage.sh'))

if gwcnc_env['OS'] == 'linux':
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$GWCNC_DISTDIR/lib/*.so'))
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$DISTDIR/cpp/lib/*.so'))
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$DISTDIR/about/lib/*.so'))
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$DISTDIR/services_common/lib/*'))
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$DISTDIR/notification/lib/*'))
    gwcnc_env.Install('$GWCNC_DISTDIR/tar/lib', gwcnc_env.Glob('$DISTDIR/config/lib/*.so'))

Return('prog')
