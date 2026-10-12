# Builds and runs only the server. Config and key live in /data, mount a volume there so they survive updates.
FROM debian:trixie AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config ca-certificates \
        autoconf autoconf-archive automake libtool \
    && rm -rf /var/lib/apt/lists/*

# Same vcpkg commit as the Windows dev machine, so both build the same library versions
RUN git clone https://github.com/microsoft/vcpkg /vcpkg \
    && git -C /vcpkg checkout eaca4a577b6b678c6e10252754b6988a61746c19 \
    && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build --target yapper_server

FROM debian:trixie-slim
COPY --from=build /src/build/sources/server/yapper_server /usr/local/bin/yapper_server
WORKDIR /data
VOLUME /data
EXPOSE 5688/tcp 5688/udp
CMD ["yapper_server"]
