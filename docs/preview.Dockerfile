FROM readthedocs/build:6.0

ENV A0_DIR=/home/docs/alephzero
ENV VENV_DIR=/home/docs/checkouts/readthedocs.org/user_builds/alephzero/envs/latest

RUN python3.7 -m virtualenv $VENV_DIR

ENV VENV_BIN=$VENV_DIR/bin
ENV VENV_PYTHON=$VENV_BIN/python
ENV VENV_PIP="$VENV_PYTHON -m pip install --upgrade --no-cache-dir"
  
RUN $VENV_PIP pip
RUN $VENV_PIP \
    "Pygments==2.3.1" \
    "setuptools==41.0.1" \
    "docutils==0.14" \
    "mock==1.0.1" \
    "pillow==5.4.1" \
    "alabaster>=0.7,<0.8,!=0.7.5" \
    "commonmark==0.8.1" \
    "recommonmark==0.5.0" \
    "sphinx<2" \
    "sphinx-rtd-theme<0.5" \
    "readthedocs-sphinx-ext<1.1"
RUN $VENV_PIP "breathe==4.15.0"
RUN $VENV_PIP sphinx-server gunicorn
