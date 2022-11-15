FROM docker.io/cyd01/cross-gcc:latest

USER root

ARG  HOST_USER
ARG  HOST_HOME
ARG  HOST_SHELL

RUN  usermod --login ${HOST_USER} --home ${HOST_HOME} --move-home --shell ${HOST_SHELL} ${DEFAULT_USER} \
     && groupmod --new-name ${HOST_USER} ${DEFAULT_USER}                          \
     && usermod --groups root ${HOST_USER} \
     && echo "${HOST_USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/10-user
  
ENV  DEFAULT_USER=${HOST_USER}

USER ${DEFAULT_USER}

COPY ./scripts/ /usr/local/scripts/
