defmodule HeartTest.MixProject do
  use Mix.Project

  def project do
    [
      app: :heart_test,
      version: "0.1.0",
      elixir: "~> 1.14",
      start_permanent: Mix.env() == :prod,
      compilers: [:elixir_make | Mix.compilers()],
      make_targets: ["all"],
      make_clean: ["mix_clean"],
      make_error_message: "",
      deps: deps(),
      dialyzer: dialyzer(),
      preferred_cli_env: %{
        dialyzer: :dev,
        credo: :dev
      }
    ]
  end

  # Run "mix help compile.app" to learn about applications.
  def application do
    [
      extra_applications: [:logger]
    ]
  end

  defp deps do
    [
      {:elixir_make, "~> 0.6", runtime: false},
      {:dialyxir, "~> 1.2", only: :dev, runtime: false},
      {:credo, "~> 1.2", only: :dev, runtime: false}
    ]
  end

  defp dialyzer() do
    [
      flags: [:missing_return, :extra_return, :unmatched_returns, :error_handling, :underspecs]
    ]
  end
end
