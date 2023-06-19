target "binary-cross" {
  inherits = ["binary", "_platforms"]
  platforms = [
    "linux/arm/v7"
  ]
}
