<!-- omit in toc -->
# Regarding contributions

All types of contributions are encouraged and valued. See the [Table of contents](#table-of-contents) for different ways to help and details about how this project handles them. Please make sure to read the relevant section before making your contribution. It will make it a lot easier for us maintainers and smooth out the experience for all involved. We look forward to your contributions.

> And if you like the project, but just don't have time to contribute, that's fine. There are other easy ways to support the project and show your appreciation, which we would also be very happy about:
>
> - Star the project
> - Tweet about it
> - Refer this project in your project's readme
> - Mention the project at local meetups and tell your friends/colleagues

<!-- omit in toc -->
## Table of contents

- [I have a question](#i-have-a-question)
- [I want to contribute](#i-want-to-contribute)
  - [Reporting bugs](#reporting-bugs)
    - [Before submitting a bug report](#before-submitting-a-bug-report)
    - [How do I submit a good bug report?](#how-do-i-submit-a-good-bug-report)
  - [Suggesting enhancements](#suggesting-enhancements)
    - [Before Submitting an Enhancement](#before-submitting-an-enhancement)
    - [How do I submit a good enhancement suggestion?](#how-do-i-submit-a-good-enhancement-suggestion)
  - [Your first code contribution](#your-first-code-contribution)
  - [Test of codebase](#test-of-codebase)
  - [Lint of codebase](#lint-of-codebase)
    - [Linters in GitHub Action](#linters-in-github-action)
    - [Run super-linter locally](#run-super-linter-locally)
    - [Run super-linter interactively](#run-super-linter-interactively)

## I have a question

Before you ask a question, it is best to search for existing [issues][issues] that might help you. In case you have found a suitable issue and still need clarification, you can write your question in this issue. It is also advisable to search the internet for answers first.

If you then still feel the need to ask a question and need clarification, please
follow the steps in [Reporting bugs](#reporting-bugs).

## I want to contribute

### Reporting bugs

#### Before submitting a bug report

A good bug report shouldn't leave others needing to chase you up for more information. Therefore, we ask you to investigate carefully, collect information and describe the issue in detail in your report. Please complete the following steps in advance to help us fix any potential bug as fast as possible:

- Make sure that you are using the latest version.
- Determine if your bug is really a bug and not an error on your side e.g. using incompatible environment components/versions.
- To see if other users have experienced (and potentially already solved) the same issue you are having, check if there is not already a bug report existing for your bug or error in the [bug tracker][issues_bugs].
- Also make sure to search the internet to see if users outside of the GitHub community have discussed the issue.
- Collect information about the bug:
  - Axis device model
  - Axis device firmware version
  - Stack trace
  - OS and version (Windows, Linux, macOS, x86, ARM)
  - Version of the interpreter, compiler, SDK, runtime environment, package manager, depending on what seems relevant
  - Possibly your input and the output
  - Can you reliably reproduce the issue? And can you also reproduce it with older versions?

#### How do I submit a good bug report?

We use GitHub issues to track bugs and errors. If you run into an issue with the project:

- Open an [issue][issues_new].
- Explain the behavior you would expect and the actual behavior.
- Please provide as much context as possible and describe the *reproduction steps* that someone else can follow to recreate the issue on their own.
- Provide the information you collected in the previous section.

Once it's filed:

- The project team will label the issue accordingly.
- A team member will try to reproduce the issue with your provided steps. If there are no reproduction steps or no obvious way to reproduce the issue, the team will ask you for those steps. Bugs without steps will not be addressed until they can be reproduced.
- If the team is able to reproduce the issue, it will be prioritized according to severity.

### Suggesting enhancements

This section guides you through submitting an enhancement suggestion, **including completely new features and minor improvements to existing functionality**. Following these guidelines will help maintainers and the community to understand your suggestion and find related suggestions.

#### Before Submitting an Enhancement

- Make sure that you are using the latest version.
- Read the documentation carefully and find out if the functionality is already covered, maybe by an individual configuration.
- Perform a [search][issues] to see if the enhancement has already been suggested. If it has, add a comment to the existing issue instead of opening a new one.
- Find out whether your idea fits with the scope and aims of the project. Keep in mind that we want features that will be useful to the majority of our users and not just a small subset.

#### How do I submit a good enhancement suggestion?

Enhancement suggestions are tracked as [GitHub issues][issues].

- Use a **clear and descriptive title** for the issue to identify the suggestion.
- Provide a **step-by-step description of the suggested enhancement** in as many details as possible.
- **Describe the current behavior** and **explain which behavior you expected to see instead** and why. At this point you can also tell which alternatives do not work for you.
- You may want to **include screenshots and animated GIFs** which help you demonstrate the steps or point out the part which the suggestion is related to.
- **Explain why this enhancement would be useful** to most users. You may also want to point out the other projects that solved it better and which could serve as inspiration.

### Your first code contribution

Start by [forking the repository](https://docs.github.com/en/github/getting-started-with-github/fork-a-repo), i.e. copying the repository to your account to grant you write access. Continue with cloning the forked repository to your local machine.

```sh
git clone https://github.com/<your username>/AxisCommunications/acap-runtime.git
```

Navigate into the cloned directory and create a new branch:

```sh
cd acap-runtime
git switch -c <branch name>
```

Update the code according to your requirements, and commit the changes using the [conventional commits](https://www.conventionalcommits.org) message style:

```sh
git commit -a -m 'Follow the conventional commit messages style to write this message'
```

Continue with pushing the local commits to GitHub:

```sh
git push origin <branch name>
```

Before opening a Pull Request (PR), please consider the following guidelines:

- Please make sure that the code builds perfectly fine on your local system.
- Make sure that all linters pass, see [Lint of codebase](#lint-of-codebase)
- The PR will have to meet the code standard already available in the repository.
- Explanatory comments related to code functions are required. Please write code comments for a better understanding of the code for other developers.
- Note that code changes or additions to the `.github` folder (or sub-folders) will not be accepted.

And finally when you are satisfied with your changes, open a new PR.

### Test of codebase

The repo has a CI/CD workflow setup that includes testing of the codebase. The steps of the workflow are, for each architecture:

1. Build the ACAP Runtime [test suite][test-suite] Docker image and push to Docker Hub.
2. Pull the test suite Docker image and run it on an external device.
3. Build the ACAP Runtime Docker image and push to Docker Hub.
4. Build the ACAP Runtime containerized Docker image and push to Docker Hub.

The workflow should be possible to run from a fork with the following updates:

- Create your own Docker repository and refer to it in the workflow. Make sure to add your own secrets for login to the repository.
- Setup devices to test on and update the workflow with their IP addresses and provide secrets for the user name and login to the devices.

### Lint of codebase

A set of different linters test the codebase and these must pass in order to get a pull request approved.

#### Linters in GitHub Action

When you create a pull request, a set of linters will run syntax and format checks on different file types in GitHub actions by making use of a tool called [super-linter][super-linter]. If any of the linters gives an error, this will be shown in the action connected to the pull request.

In order to speed up development, it's possible to run linters as part of your local development environment.

#### Run super-linter locally

Since super-linter is using a Docker image in GitHub Actions, users of other editors may run it locally to lint the codebase. For complete instructions and guidance, see super-linter page for [running locally][super-linter-local].

To run a number of linters on the codebase from command line:

```sh
docker run --rm \
  -v $PWD:/tmp/lint \
  -e RUN_LOCAL=true \
  -e LINTER_RULES_PATH=/ \
  -e VALIDATE_BASH=true \
  -e VALIDATE_DOCKERFILE_HADOLINT=true \
  -e VALIDATE_MARKDOWN=true \
  -e VALIDATE_SHELL_SHFMT=true \
  -e VALIDATE_YAML=true \
  github/super-linter:slim-v4
```

See [`.github/workflows/linter.yml`](.github/workflows/linter.yml) for the exact setup used by this project.

#### Run super-linter interactively

It might be more convenient to run super-linter interactively. Run container and enter command line:

```sh
docker run --rm \
  -v $PWD:/tmp/lint \
  -w /tmp/lint \
  --entrypoint /bin/bash \
  -it github/super-linter:slim-v4
```

Then from the container terminal, the following commands can lint the the code base for different file types:

```sh
# Lint Dockerfile files
hadolint $(find -type f -name "Dockerfile*")

# Lint Markdown files
markdownlint .

# Lint YAML files
yamllint .

# Lint shell script files
shellcheck $(shfmt -f .)
shfmt -d .
```

To lint only a specific file, replace `.` or `$(COMMAND)` with the file path.

<!-- markdownlint-disable MD034 -->
[issues]: https://github.com/AxisCommunications/acap-runtime/issues
[issues_new]: https://github.com/AxisCommunications/acap-runtime/issues/new
[issues_bugs]: https://github.com/AxisCommunications/acap-runtime/issues?q=label%3Abug
[super-linter]: https://github.com/github/super-linter
[super-linter-local]: https://github.com/github/super-linter/blob/main/docs/run-linter-locally.md
[test-suite]: https://github.com/AxisCommunications/acap-runtime#test-suite
<!-- markdownlint-enable MD034 -->
