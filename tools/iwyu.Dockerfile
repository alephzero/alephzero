FROM alpine:3.10

RUN apk add clang-dev clang-static cmake g++ git linux-headers llvm8-dev llvm8-static make python wget

RUN cd / && \
    git clone https://github.com/rizsotto/Bear.git && \
    mkdir -p /Bear/build && \
    cd /Bear/build && \
    cmake .. && \
    make install -j

RUN cd / && \
    git clone https://github.com/include-what-you-use/include-what-you-use.git && \
    mkdir -p /include-what-you-use/build && \
    cd /include-what-you-use/build && \
    git checkout clang_8.0 && \
    cmake -G "Unix Makefiles" -DCMAKE_PREFIX_PATH=/usr/lib/llvm8 .. && \
    make install -j
