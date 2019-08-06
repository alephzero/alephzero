#########
# Build #
#########
FROM alpine:latest as builder

RUN apk update && apk add git build-base ninja cmake python3

RUN git clone --depth 1 https://github.com/llvm/llvm-project.git
RUN mv /llvm-project/clang /llvm-project/llvm/tools
RUN mv /llvm-project/libcxx /llvm-project/llvm/projects

WORKDIR /llvm-project/llvm/build
RUN cmake -GNinja -DLLVM_BUILD_STATIC=ON -DLLVM_ENABLE_LIBCXX=ON -DCMAKE_BUILD_TYPE=MINSIZEREL ..
RUN ninja clang-format

##########
# Deploy #
##########
FROM scratch

COPY --from=builder /llvm-project/llvm/build/bin/clang-format /

ENTRYPOINT ["/clang-format"]
CMD ["--help"]