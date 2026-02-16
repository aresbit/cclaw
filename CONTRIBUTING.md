# Contributing to CClaw

Thank you for your interest in contributing to CClaw! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

Please be respectful and considerate of others when contributing to this project. We aim to foster an inclusive and welcoming community.

## Development Environment

### Prerequisites

- C11 compatible compiler (clang 10+ or gcc 9+ recommended)
- Git
- Make or CMake
- Development libraries:
  - libcurl
  - libsqlite3
  - libsodium
  - libuv

### Setting Up

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/your-username/cclaw.git
   cd cclaw
   ```
3. Set up the development environment:
   ```bash
   make setup
   ```
4. Build the project:
   ```bash
   make debug=1
   ```
5. Run tests to ensure everything works:
   ```bash
   make test
   ```

## Workflow

### 1. Issue Tracking
- Check existing issues before creating new ones
- Use descriptive titles and detailed descriptions
- Reference related issues and pull requests
- Label issues appropriately (bug, enhancement, documentation, etc.)

### 2. Branch Strategy
- Create a new branch for each feature or bug fix
- Use descriptive branch names:
  - `feature/description` for new features
  - `fix/description` for bug fixes
  - `docs/description` for documentation
  - `refactor/description` for refactoring

### 3. Commit Messages
Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, missing semi-colons, etc.)
- `refactor`: Code refactoring
- `test`: Adding or updating tests
- `chore`: Maintenance tasks

Examples:
```
feat(provider): add OpenAI provider implementation

fix(memory): fix memory leak in SQLite backend

docs(readme): update installation instructions
```

### 4. Pull Requests
- Keep PRs focused on a single change
- Include tests for new functionality
- Update documentation as needed
- Ensure all tests pass
- Request review from maintainers

## Coding Standards

### C Language
- Use C11 standard features
- Follow `sp.h` best practices (see spclib skill)
- Zero warnings policy
- No compiler warnings in release builds

### Memory Management
- All allocations must go through allocator interfaces
- Use `SP_ZERO_INITIALIZE()` for structure initialization
- Check for NULL returns from allocation functions
- Clean up resources in reverse order of acquisition

### Error Handling
- Use `err_t` error codes consistently
- Provide meaningful error messages
- Propagate errors up the call stack
- No silent failures

### Code Style
- Use `clang-format` with project configuration
- 4 spaces for indentation (no tabs)
- 100 character line limit
- Braces on same line for functions, new line for control structures

### Documentation
- Document all public APIs
- Use Doxygen-style comments for functions
- Include examples for complex functionality
- Update README.md for user-facing changes

## Testing

### Writing Tests
- Write unit tests for new functionality
- Test edge cases and error conditions
- Mock external dependencies
- Keep tests fast and isolated

### Running Tests
```bash
# Run all tests
make test

# Run specific test
./bin/test_cclaw --gtest_filter=TestSuite.TestName

# Run with valgrind
make check
```

### Test Structure
- Tests go in the `tests/` directory
- Use the test framework provided in `tests/framework.h`
- Group related tests into test suites
- Clean up after each test

## Security Guidelines

### Input Validation
- Validate all external inputs
- Sanitize file paths and URLs
- Check buffer boundaries
- Use secure string functions

### Cryptography
- Use libsodium for cryptographic operations
- Never roll your own crypto
- Store secrets securely
- Use constant-time comparisons for sensitive data

### Resource Management
- Limit resource usage (memory, CPU, file descriptors)
- Implement timeouts for network operations
- Clean up temporary files
- Use sandboxing where appropriate

## Performance Considerations

### Optimization Priorities
1. Correctness
2. Security
3. Maintainability
4. Performance

### Performance Guidelines
- Profile before optimizing
- Focus on algorithmic complexity first
- Cache expensive computations
- Use appropriate data structures
- Minimize memory allocations

## Documentation

### API Documentation
- Document function parameters and return values
- Include usage examples
- Note thread safety and reentrancy
- Document memory ownership

### User Documentation
- Update README.md for user-facing changes
- Add examples to `examples/` directory
- Document configuration options
- Include troubleshooting guides

### Internal Documentation
- Document complex algorithms
- Explain architectural decisions
- Note known limitations
- Include TODO comments for future work

## Review Process

### Code Review Checklist
- [ ] Code follows project standards
- [ ] Tests are included and pass
- [ ] Documentation is updated
- [ ] No security issues
- [ ] Performance considerations addressed
- [ ] Backward compatibility maintained

### Review Etiquette
- Be constructive and respectful
- Focus on the code, not the person
- Explain reasoning for suggestions
- Acknowledge good practices

## Release Process

### Versioning
Follow [Semantic Versioning](https://semver.org/):
- MAJOR: Incompatible API changes
- MINOR: Backward-compatible functionality
- PATCH: Backward-compatible bug fixes

### Release Checklist
- [ ] All tests pass
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] Version numbers updated
- [ ] Release notes prepared
- [ ] Binaries built and tested

## Getting Help

- Check existing documentation
- Search existing issues
- Ask in discussions
- Contact maintainers

## Recognition

Contributors will be credited in:
- CHANGELOG.md
- CONTRIBUTORS.md
- Release notes

Thank you for contributing to CClaw! 🦀