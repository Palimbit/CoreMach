# Private CoreMach repository policy

At initial extraction, Palimbit has GitHub Free for organizations and CoreMach
is private. Repository Actions is disabled; this is kept unchanged.

The prepared workflow runs only a Linux engine validation by default if Actions
is later enabled. Windows/MSVC, macOS, Qt6 and Windows packaging require manual
full_matrix selection. Package upload is a separate opt-in with one-day retention.
No scheduled jobs or automatic release publication are configured.

GitHub Free for organizations includes 2000 Actions minutes/month and 500 MB
of artifact storage, shared with Packages. These are not guarantees of remaining
quota: other organization repositories may consume them. No billing budget,
subscription, runner purchase or organization policy is changed by this extraction.
See https://docs.github.com/en/billing/concepts/product-billing/github-actions.

Private branch protection for organizations requires Team or Enterprise; it is
not assumed here. Passing checks must therefore be reviewed before merging.
See https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches.

CodeQL is not enabled for this private Free repository. Private code scanning
requires an eligible organization plan and GitHub Code Security entitlement.
No paid security feature is requested.
See https://docs.github.com/en/code-security/reference/code-scanning/troubleshoot-analysis-errors/private-repository-enablement.

Source Git history storage is distinct from the Actions artifact allowance.
The filtered-history candidate preserves code authorship with a versioned
commit map while excluding inherited firmware, generated executables and state.
