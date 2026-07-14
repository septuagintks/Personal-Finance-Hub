FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_BUILD_TYPE=Release

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        default-libmysqlclient-dev \
        libargon2-dev \
        libbrotli-dev \
        libdrogon-dev \
        libgtest-dev \
        libhiredis-dev \
        libjsoncpp-dev \
        libpq-dev \
        libsqlite3-dev \
        libspdlog-dev \
        libssl-dev \
        libyaml-cpp-dev \
        ninja-build \
        nlohmann-json3-dev \
        python3 \
        tzdata \
        uuid-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH)" \
    && cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
        -DPFH_BUILD_POSTGRESQL=ON \
        -DMYSQL_LIBRARIES="/usr/lib/${multiarch}/libmysqlclient.so" \
    && cmake --build /build --target pfh_server --parallel 1

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libargon2-1 \
        libdrogon1t64 \
        libpq5 \
        libspdlog1.12 \
        tzdata \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --home-dir /app --shell /usr/sbin/nologin pfh

WORKDIR /app
COPY --from=build --chown=pfh:pfh /build/pfh_server /app/pfh_server
COPY --chown=pfh:pfh config/config.example.json /app/config/config.example.json

USER pfh
EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=20s --retries=6 \
    CMD curl --fail --silent --show-error \
        http://127.0.0.1:8080/api/v1/currencies >/dev/null || exit 1

ENTRYPOINT ["/app/pfh_server"]
