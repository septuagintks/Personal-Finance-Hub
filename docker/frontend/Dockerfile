FROM node:24.15.0-alpine AS build

WORKDIR /src
RUN corepack enable

COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY frontend/package.json frontend/package.json
RUN corepack pnpm install --frozen-lockfile

COPY frontend frontend
RUN corepack pnpm --dir frontend build

FROM nginx:1.27-alpine

COPY --from=build /src/frontend/dist /usr/share/nginx/html
COPY docker/frontend/nginx.conf /etc/nginx/conf.d/default.conf

RUN addgroup -S pfh && adduser -S -G pfh pfh \
    && chown -R pfh:pfh /usr/share/nginx/html /var/cache/nginx /var/log/nginx /etc/nginx/conf.d \
    && touch /var/run/nginx.pid \
    && chown pfh:pfh /var/run/nginx.pid

USER pfh
EXPOSE 8080
