# Contributing to reflector-native

## Development Setup

### Prerequisites
- Linux: See `scripts/setup-linux.sh`
- macOS: Xcode Command Line Tools

### Building
```bash
make
```

### Testing
```bash
# Run reflector
sudo ./reflector-macos en0  # macOS
sudo ./reflector-linux eth0  # Linux

# Verify functionality with network test tool
```

## Git Workflow

### Branching Strategy
- `main` - stable releases only
- `develop` - active development
- `feature/*` - new features
- `bugfix/*` - bug fixes
- `hotfix/*` - urgent production fixes

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation
- `style`: Formatting
- `refactor`: Code restructuring
- `perf`: Performance improvement
- `test`: Tests
- `build`: Build system
- `ci`: CI/CD
- `chore`: Maintenance

**Examples:**
```bash
feat(xdp): add support for VLAN tagging

Implement 802.1Q VLAN tag parsing and preservation in
reflected packets for tagged network environments.

Closes #42
```

```bash
fix(bpf): correct buffer overflow in macOS receive path

The BPF read buffer was not properly bounds-checked,
causing crashes with jumbo frames. Added validation.

Fixes #38
```

### Setting Up Git Commit Template

```bash
git config commit.template .gitmessage
```

## Code Style

### C Code
- Use kernel style for C code
- 4-space indentation (tabs)
- Max 100 characters per line
- Comment complex logic
- Document functions with block comments

### Go Code
- Follow standard Go formatting (`gofmt`)
- Use meaningful variable names
- Add godoc comments for exported functions

## Testing

### Manual Testing
1. Build on target platform
2. Start reflector on test interface
3. Send ITO packets from test tool
4. Verify reflection in statistics
5. Check performance metrics

### Performance Testing
```bash
# Monitor statistics
watch -n 1 'grep "pps" /var/log/reflector.log'

# Check CPU usage
top -H -p $(pgrep reflector)

# Linux: Check XDP stats
sudo bpftool prog show
sudo bpftool map dump name stats_map
```

## Pull Request Process

1. **Fork and clone** the repository
2. **Create a branch** from `develop`
3. **Make changes** with clear commits
4. **Test thoroughly** on your platform
5. **Update documentation** if needed
6. **Submit PR** to `develop` branch

### PR Checklist
- [ ] Code compiles without warnings
- [ ] Tested on target platform(s)
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] Commit messages follow convention
- [ ] No debug/test code left in

## Versioning

We use [Semantic Versioning](https://semver.org/):

- **MAJOR**: Incompatible API changes
- **MINOR**: New functionality (backward compatible)
- **PATCH**: Bug fixes (backward compatible)

## Release Process

1. Update `VERSION` file
2. Update `CHANGELOG.md`
3. Commit: `chore(release): bump version to X.Y.Z`
4. Tag: `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
5. Push: `git push origin main --tags`

## Code of Conduct

- Be respectful and constructive
- Focus on what's best for the project
- Welcome newcomers
- Report issues responsibly

## Questions?

Open an issue for:
- Bug reports
- Feature requests
- Documentation improvements
- General questions

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
