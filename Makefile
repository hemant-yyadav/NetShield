.PHONY: build up down logs reset

build:
	docker-compose build

up:
	docker-compose up -d

down:
	docker-compose down

logs:
	docker-compose logs -f

reset:
	docker-compose down -v
	docker-compose build
