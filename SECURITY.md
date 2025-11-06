# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

We take the security of reflector-native seriously. If you believe you have found a security vulnerability, please report it to us as described below.

**Please do not report security vulnerabilities through public GitHub issues.**

Instead, please report them via email to:
- **Email:** [security@yourdomain.com] (replace with your actual security contact)

You should receive a response within 48 hours. If for some reason you do not, please follow up via email to ensure we received your original message.

Please include the following information:

- Type of issue (e.g., buffer overflow, privilege escalation, etc.)
- Full paths of source file(s) related to the issue
- The location of the affected source code (tag/branch/commit or direct URL)
- Any special configuration required to reproduce the issue
- Step-by-step instructions to reproduce the issue
- Proof-of-concept or exploit code (if possible)
- Impact of the issue, including how an attacker might exploit it

This information will help us triage your report more quickly.

## Security Considerations

### Network Security

- **Raw Socket Access**: This application requires raw socket access (root/sudo) to capture and inject packets at the link layer
- **No Authentication**: The reflector does not implement authentication - rely on network-level security
- **No Encryption**: Packets are reflected as-is without encryption
- **DoS Protection**: No built-in rate limiting - can be overwhelmed by packet floods

### Deployment Best Practices

1. **Isolated Networks**: Deploy in isolated test networks, not production
2. **Firewall Rules**: Use firewall rules to limit which hosts can send packets
3. **Monitoring**: Monitor for unexpected traffic patterns
4. **Updates**: Keep the software and OS up to date
5. **Permissions**: Run with minimum required privileges where possible

### Known Limitations

- Designed for trusted test environments (lab/test networks)
- Not hardened for hostile network environments
- No input validation on reflected packets (intentional for test tool)
- Processes all packets matching ITO signature without authentication

## Security Testing

We use the following security measures:

- **CodeQL** - Automated code scanning for vulnerabilities
- **Gitleaks** - Secret detection in code and commits
- **cppcheck** - Static analysis for C code
- **Pre-commit hooks** - Secret detection before commits
- **Dependency scanning** - Regular checks for vulnerable dependencies

## Disclosure Policy

When we receive a security bug report, we will:

1. Confirm the problem and determine affected versions
2. Audit code to find similar problems
3. Prepare fixes for all supported versions
4. Release patched versions as soon as possible

## Comments on this Policy

If you have suggestions on how this process could be improved, please submit a pull request.
