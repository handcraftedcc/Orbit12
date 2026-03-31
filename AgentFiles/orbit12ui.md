# Orbit12 UI System

## Overview
JSON-driven UI with parameter list

## Parameter Schema
{
  "name": "param_name",
  "label": "Display Name",
  "type": "int",
  "default": 0
}

## Types
- Int
- Float
- Boolean
- Enum
- String
- Rate
- Note
- Button
- Folder

## Conditional Visibility
"viscondition": "param2 < 5"

## Interaction
- encoder rotate → navigate
- encoder press → edit/select

## Rendering
- label left
- value right
- selected highlighted
