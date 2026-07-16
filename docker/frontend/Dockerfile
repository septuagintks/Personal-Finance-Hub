FROM nginx:1.27-alpine

COPY frontend/dist /usr/share/nginx/html
COPY docker/frontend/nginx.conf /etc/nginx/conf.d/default.conf

RUN addgroup -S pfh && adduser -S -G pfh pfh \
    && chown -R pfh:pfh /usr/share/nginx/html /var/cache/nginx /var/log/nginx /etc/nginx/conf.d \
    && touch /var/run/nginx.pid \
    && chown pfh:pfh /var/run/nginx.pid

USER pfh
EXPOSE 8080
