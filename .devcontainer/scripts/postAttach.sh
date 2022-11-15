#!/bin/bash

DEBUG=
[ -n "${SCRIPTS_DEBUG}" ] && [ "${SCRIPTS_DEBUG}" != "0" ] && DEBUG=1
[ -z "$DEBUG" ] && printf -- "\n" || printf -- "\e[39m| DEBUG | executing $0\e[39m\n"


test -r ~/host/.devcontainer_rc \
&& { 
    [ -z "$DEBUG" ] || printf -- "\e[39m| DEBUG | checking for ~/host/.devcontainer_rc...\e[39m\n"
    . ~/host/.devcontainer_rc \
    && printf -- "\e[92m| INFO | ~/host/.devcontainer_rc sucessfully executed\e[39m\n" \
    ||  printf -- "\e[91m| ERROR |  ~/host/.devcontainer_rc  \e[39m\n"
} \
|| {
    printf -- "\e[93m| WARN | no .devcontainer_rc file found in your home: create one to tune your configuration\e[39m\n";
}

HOST_FILES=${HOST_FILES:-default}
[ -z "$DEBUG" ] || printf -- "\e[39m| DEBUG | checking for HOST_FILES (${HOST_FILES})...\e[39m\n"
HOST_FILES_FINAL=
for f in ${HOST_FILES}; do
    [ "${f}" = "default" ] \
        && {
            [ -z "${HISTFILE}" ] \
            && printf -- "\e[93m| WARN | HISTFILE not defined : not exported on your host ?\e[39m\n" \
            ||  HOST_FILES_FINAL="${HOST_FILES_FINAL} ${HISTFILE#${HOME}/}"
            HOST_FILES_FINAL="${HOST_FILES_FINAL} .netrc .makerc"
        } \
        || HOST_FILES_FINAL="${HOST_FILES_FINAL} ${f}"
done
for f in ${HOST_FILES_FINAL}; do
    hostf="${HOME}/host/${f#${HOME}/}"
    [ -e ${hostf} ] \
        && {
            cd ~ && ln -s -f ${hostf} ${f} \
            && printf -- "\e[92m| INFO | link for ${f} sucessfully created\e[39m\n" \
            || printf -- "\e[91m| ERROR | failed to create link for ${f}\e[39m\n"    
        } \
        || {
            printf -- "\e[93m| WARN | ${f} link not created: host file (${hostf}) not found\e[39m\n"
        }
done

printf -- "\e[92m| INFO | $0 executed\e[39m\n"

test -d $HOME/host && {
  test -d /builds && sudo rm -rf /builds
  mkdir -p $HOME/host/builds && sudo ln -sf $HOME/host/builds /builds
  test -d /sources && sudo rm -rf /sources
  mkdir -p $HOME/host/sources && sudo ln -sf $HOME/host/sources /sources
}