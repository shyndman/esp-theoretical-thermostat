# Copilot Code Review Guidance

When providing code review assistance on this repository:

1. Always cross-reference the implementation under review with the documentation located at `.ai/openspec/changes/{name-of-this-pr's-change}/**`, where `{name-of-this-pr's-change}` typically matches the branch name or the change name called out in the PR description.
2. Call out any discrepancies between the implemented code and the referenced documentation, noting the specific files or sections involved.
3. If the documentation is ambiguous, highlight the uncertainty and suggest clarifying updates alongside the code comments.
4. Avoid approving changes until the implementation and documentation are clearly aligned or the differences are intentionally justified in the review notes.
