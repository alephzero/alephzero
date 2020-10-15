#!/bin/bash

cd "$(dirname "$0")"

if [[ "$(docker images -q a0_doc_preview_image 2> /dev/null)" == "" ]]; then
  docker build -t a0_doc_preview_image -f preview.Dockerfile .
fi

docker run \
  --rm \
  -it \
  -d \
  --name a0_doc_preview \
  -p 8000:8000 \
  a0_doc_preview_image bash

shutdown()
{
  docker rm -f a0_doc_preview
}

trap 'shutdown' INT  # ctrl+c handler

docker cp ${PWD}/.. a0_doc_preview:/home/docs/alephzero
docker exec -u root a0_doc_preview bash -c "chown docs:docs -R /home/docs/alephzero"

docker exec a0_doc_preview bash -c 'echo -e "\nhtml_theme = \"sphinx_rtd_theme\"\nextensions += [\"readthedocs_ext.readthedocs\"]\n" >> /home/docs/alephzero/docs/conf.py'

docker exec a0_doc_preview bash -c '\
  $VENV_PIP --exists-action=w -r $A0_DIR/docs/requirements.txt && \
  cd $A0_DIR/docs && $VENV_PYTHON $VENV_BIN/sphinx-build -T -E -b readthedocs -d _build/doctrees-readthedocs -D language=en . _build/html && \
  cd $A0_DIR/docs/_build/html/ && python3 -m http.server
'

shutdown
