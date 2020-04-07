#!/bin/bash

cd "$(dirname "$0")"

docker run \
  --rm \
  -it \
  -d \
  --name a0_preview \
  -p 8002:8000 \
  readthedocs/build:6.0 bash

shutdown()
{
  docker rm -f a0_preview
}

trap 'shutdown' INT  # ctrl+c handler

docker cp ${PWD}/.. a0_preview:/home/docs/alephzero
docker exec -u root a0_preview bash -c "chown docs:docs -R /home/docs/alephzero"

docker exec a0_preview bash -c 'echo -e "\nhtml_theme = \"sphinx_rtd_theme\"\nextensions += [\"readthedocs_ext.readthedocs\"]\n" >> /home/docs/alephzero/docs/conf.py'

docker exec a0_preview bash -c '\
  A0_DIR=/home/docs/alephzero; \
  VENV_DIR=/home/docs/checkouts/readthedocs.org/user_builds/alephzero/envs/latest; \
  python3.7 -m virtualenv $VENV_DIR; \
  VENV_BIN=$VENV_DIR/bin; \
  VENV_PYTHON=$VENV_BIN/python; \
  VENV_PIP="$VENV_PYTHON -m pip install --upgrade --no-cache-dir"; \
  $VENV_PIP pip; \
  $VENV_PIP \
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
    "readthedocs-sphinx-ext<1.1"; \
  $VENV_PIP --exists-action=w -r $A0_DIR/docs/requirements.txt; \
  cd $A0_DIR/docs && $VENV_PYTHON $VENV_BIN/sphinx-build -T -E -b readthedocs -d _build/doctrees-readthedocs -D language=en . _build/html; \
  $VENV_PIP sphinx-server gunicorn; \
  $VENV_BIN/gunicorn -w 1 "sphinxserver:app(home=\"$A0_DIR/docs/_build/html/\")" -b 0.0.0.0:8000
'

shutdown
