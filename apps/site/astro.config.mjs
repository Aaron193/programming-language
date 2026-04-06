// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

const site = process.env.SITE_URL || 'https://moglang.dev';
const editBaseUrl = process.env.MOG_SITE_EDIT_BASE_URL;

export default defineConfig({
	site,
	integrations: [
		starlight({
			title: 'Mog',
			description:
				'Mog is a strictly typed, bytecode-compiled programming language with a VM, REPL, native packages, and editor tooling.',
			tagline: 'A sharp programming language for people who want the toolchain to take them seriously.',
			logo: {
				src: './src/assets/mog-mark.svg',
				alt: 'Mog',
			},
			customCss: ['./src/styles/site.css'],
			favicon: '/favicon.svg',
			pagefind: true,
			lastUpdated: true,
			disable404Route: true,
			editLink: editBaseUrl ? { baseUrl: editBaseUrl } : undefined,
			head: [
				{
					tag: 'meta',
					attrs: {
						name: 'theme-color',
						content: '#061311',
					},
				},
			],
			sidebar: [
				{
					label: 'Overview',
					items: [
						{ label: 'Docs Home', slug: 'docs' },
					],
				},
				{
					label: 'Getting Started',
					items: [
						{ label: 'Install and Build', slug: 'docs/getting-started/install' },
						{ label: 'Quickstart', slug: 'docs/getting-started/quickstart' },
					],
				},
				{
					label: 'Language',
					items: [
						{ label: 'Basics', slug: 'docs/language/basics' },
						{
							label: 'Functions, Types, and Modules',
							slug: 'docs/language/functions-types-modules',
						},
					],
				},
				{
					label: 'Tooling',
					items: [{ label: 'CLI, REPL, VS Code, and LSP', slug: 'docs/tooling/cli-repl-vscode-lsp' }],
				},
				{
					label: 'Packages',
					items: [{ label: 'Imports and Native Packages', slug: 'docs/packages/imports-native-packages' }],
				},
				{
					label: 'Examples',
					items: [{ label: 'Example Programs', slug: 'docs/examples' }],
				},
				{
					label: 'Reference',
					items: [{ label: 'Syntax, Built-ins, and Flags', slug: 'docs/reference/syntax-builtins-flags' }],
				},
			],
		}),
	],
});
