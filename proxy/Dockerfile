FROM alpine:3.19

WORKDIR /app

# Install Alpine build dependencies (bypasses all Ubuntu APT proxy cache errors!)
RUN apk update && apk add --no-cache gcc make musl-dev sqlite-dev sqlite curl

# Copy proxy logic
COPY . .

# Compile
RUN make

# Start daemon
CMD ["./proxy"]
