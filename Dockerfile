# Multi-stage build: one image holds all three binaries; compose picks the entrypoint.
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt .
COPY src src
COPY tests tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF && cmake --build build -j

FROM ubuntu:24.04
RUN useradd -r app
COPY --from=build /src/build/matchmaker /src/build/gameserver /src/build/bot /usr/local/bin/
USER app
CMD ["matchmaker"]
