# CLAUDE.md

This file contains instructions for Claude Code to follow when working with this project.

## Linting and Typechecking Commands
```
# Add your linting/typechecking commands here, for example:
# cmake . && make
```

## Project Structure
- Order book implementation with various adaptors
- MPMQ queue that respects consuming in order of sequence number
- Support for event processing and trade journaling
- Testing infrastructure for both Order book and MPMQ queue

## Conventions
- Use C++20 features
- Follow existing code style in files