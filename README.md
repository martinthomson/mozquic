This is really to test the spec process, its a long way from usable
code and the interface is the least thought out bit (MozQuic.h). It
will set your machine afire if you have the nerve to run it there.

Right now it is capable of both ff000005 and 0xf123f0c5 and some greasing.

See https://github.com/quicwg/base-drafts/wiki/First-Implementation-Draft

based on tls -21

== Build Notes

setenv MOZQUIC_BUILD /home/mcmanus/src/mozquic

# These are for runtime
setenv LD_LIBRARY_PATH $MOZQUIC_BUILD/dist/`cat $MOZQUIC_BUILD/dist/latest`/lib
setenv MOZQUIC_NSS_CONFIG $MOZQUIC_BUILD/mozquic/sample/nss-config/

# These are used to build mozquic standalone
setenv MOZQUIC_NSS_ROOT $MOZQUIC_BUILD/
setenv MOZQUIC_NSS_PLATFORM Debug


mkdir mozquic
cd mozquic
git clone git@github.com:mcmanus/mozquic.git
git clone git@github.com:nss-dev/nss.git
hg clone https://hg.mozilla.org/projects/nspr
setenv USE_64 1
cd nss
git checkout NSS_TLS13_DRAFT19_BRANCH
make nss_build_all
cd ../mozquic
make
ls client server


