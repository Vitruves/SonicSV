#!/usr/bin/env python3
"""
CSV Test Data Generator for SonicSV Library

This script generates diverse CSV test files to thoroughly test
the SonicSV CSV parser library.
"""

import os
import random
import string
import csv
import sys
from typing import List, Dict, Any
from pathlib import Path

class CSVTestDataGenerator:
    def __init__(self, output_dir: str = "test_data"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        
    def generate_all_tests(self):
        """Generate all test CSV files"""
        print(f"Generating CSV test data in: {self.output_dir}")
        
        # Basic tests
        self.generate_basic_csv()
        self.generate_quoted_fields()
        self.generate_escaped_quotes()
        self.generate_multiline_fields()
        self.generate_empty_fields()
        
        # Different delimiters and formats
        self.generate_custom_delimiters()
        self.generate_different_line_endings()
        self.generate_whitespace_variations()
        
        # Edge cases
        self.generate_edge_cases()
        self.generate_unicode_test()
        self.generate_large_files()
        
        # Error cases
        self.generate_malformed_csv()
        
        # Performance tests
        self.generate_performance_tests()
        
        print(f"Generated {len(list(self.output_dir.glob('*.csv')))} test files")

    def generate_basic_csv(self):
        """Generate basic CSV files with simple structure"""
        # Simple 3x3 CSV
        data = [
            ["name", "age", "city"],
            ["John", "25", "New York"],
            ["Jane", "30", "London"],
            ["Bob", "35", "Paris"]
        ]
        self.write_csv("basic_simple.csv", data)
        
        # Single column
        data = [["id"], ["1"], ["2"], ["3"]]
        self.write_csv("basic_single_column.csv", data)
        
        # Single row
        data = [["col1", "col2", "col3", "col4", "col5"]]
        self.write_csv("basic_single_row.csv", data)
        
        # No header
        data = [
            ["John", "25", "New York"],
            ["Jane", "30", "London"]
        ]
        self.write_csv("basic_no_header.csv", data)
        
        # Many columns
        headers = [f"col{i}" for i in range(50)]
        row = [f"value{i}" for i in range(50)]
        data = [headers, row]
        self.write_csv("basic_many_columns.csv", data)

    def generate_quoted_fields(self):
        """Generate CSV files with quoted fields"""
        data = [
            ["name", "description", "tags"],
            ['"John Doe"', '"A person with quotes"', '"tag1,tag2"'],
            ['Jane Smith', '"Normal description"', 'simple'],
            ['"Bob Johnson"', 'Description with "embedded quotes"', '"complex,tags,here"']
        ]
        self.write_csv_raw("quoted_fields.csv", data)
        
        # All fields quoted
        data = [
            ['"name"', '"age"', '"city"'],
            ['"John"', '"25"', '"New York"'],
            ['"Jane"', '"30"', '"London"']
        ]
        self.write_csv_raw("quoted_all_fields.csv", data)
        
        # Mixed quoted and unquoted
        data = [
            ['name', '"full name"', 'age'],
            ['john', '"John Doe"', '25'],
            ['jane', '"Jane Smith"', '30']
        ]
        self.write_csv_raw("quoted_mixed.csv", data)

    def generate_escaped_quotes(self):
        """Generate CSV files with escaped quotes"""
        data = [
            ['text', 'quoted_text'],
            ['She said ""Hello""', '"He replied ""Hi there"""'],
            ['Quote: ""To be or not to be""', '"Shakespeare said ""To be or not to be""'], 
            ['""Start quote', 'End quote""'],
            ['""Both ends""', '"Middle ""quoted"" text"']
        ]
        self.write_csv_raw("escaped_quotes.csv", data)

    def generate_multiline_fields(self):
        """Generate CSV files with multiline fields"""
        data = [
            ['id', 'description', 'notes'],
            ['1', '"Line 1\nLine 2\nLine 3"', 'Single line'],
            ['2', 'Single line', '"Multi\nline\nnotes"'],
            ['3', '"First line\nSecond line"', '"More\nmulti\nline\ntext"']
        ]
        self.write_csv_raw("multiline_fields.csv", data)
        
        # Complex multiline with quotes and commas
        data = [
            ['poem'],
            ['"Roses are red,\nViolets are blue,\nCSV is ""great"",\nAnd so are you!"']
        ]
        self.write_csv_raw("complex_multiline.csv", data)

    def generate_empty_fields(self):
        """Generate CSV files with empty fields"""
        data = [
            ['col1', 'col2', 'col3'],
            ['a', '', 'c'],
            ['', 'b', ''],
            ['', '', ''],
            ['d', 'e', 'f']
        ]
        self.write_csv("empty_fields.csv", data)
        
        # Quoted empty fields
        data = [
            ['col1', 'col2', 'col3'],
            ['"a"', '""', '"c"'],
            ['""', '"b"', '""'],
            ['""', '""', '""']
        ]
        self.write_csv_raw("quoted_empty_fields.csv", data)
        
        # Trailing empty fields
        data = [
            ['col1', 'col2', 'col3', 'col4'],
            ['a', 'b', '', ''],
            ['c', '', '', ''],
            ['', '', '', '']
        ]
        self.write_csv("trailing_empty.csv", data)

    def generate_custom_delimiters(self):
        """Generate CSV files with different delimiters"""
        data = [
            ["name", "age", "city"],
            ["John", "25", "New York"],
            ["Jane", "30", "London"]
        ]
        
        # Pipe delimiter
        self.write_csv("pipe_delimited.csv", data, delimiter='|')
        
        # Tab delimiter
        self.write_csv("tab_delimited.csv", data, delimiter='\t')
        
        # Semicolon delimiter
        self.write_csv("semicolon_delimited.csv", data, delimiter=';')
        
        # Custom delimiter with quotes
        data_with_quotes = [
            ["name", "description", "value"],
            ["John", "Person with, comma", "100"],
            ["Jane", "Another, person", "200"]
        ]
        self.write_csv("pipe_with_quotes.csv", data_with_quotes, delimiter='|')

    def generate_different_line_endings(self):
        """Generate CSV files with different line endings"""
        data = [
            ["name", "value"],
            ["test1", "value1"],
            ["test2", "value2"]
        ]
        
        # Unix line endings (LF)
        self.write_csv_with_line_ending("unix_endings.csv", data, '\n')
        
        # Windows line endings (CRLF)
        self.write_csv_with_line_ending("windows_endings.csv", data, '\r\n')
        
        # Mac line endings (CR)
        self.write_csv_with_line_ending("mac_endings.csv", data, '\r')
        
        # Mixed line endings
        content = "name,value\ntest1,value1\r\ntest2,value2\rtest3,value3\n"
        with open(self.output_dir / "mixed_endings.csv", 'w', newline='') as f:
            f.write(content)

    def generate_whitespace_variations(self):
        """Generate CSV files with various whitespace scenarios"""
        # Leading/trailing spaces
        data = [
            ["name", "value"],
            [" John ", " 25 "],
            ["  Jane  ", "  30  "],
            ["\tBob\t", "\t35\t"]
        ]
        self.write_csv("whitespace_unquoted.csv", data)
        
        # Whitespace in quoted fields
        data = [
            ['"name"', '"value"'],
            ['" John "', '" 25 "'],
            ['"  Jane  "', '"  30  "']
        ]
        self.write_csv_raw("whitespace_quoted.csv", data)
        
        # Spaces around delimiters
        content = "name , value , description\nJohn , 25 , Person\nJane , 30 , Another person\n"
        with open(self.output_dir / "spaces_around_delimiters.csv", 'w') as f:
            f.write(content)

    def generate_edge_cases(self):
        """Generate edge case CSV files"""
        # Empty file
        with open(self.output_dir / "empty.csv", 'w') as f:
            pass
            
        # Only header
        data = [["col1", "col2", "col3"]]
        self.write_csv("only_header.csv", data)
        
        # Single field single row
        data = [["value"]]
        self.write_csv("single_field.csv", data)
        
        # Very long field
        long_value = "x" * 10000
        data = [["short", "long"], ["a", long_value]]
        self.write_csv("long_field.csv", data)
        
        # Many empty lines
        content = "name,value\n\n\n\nJohn,25\n\n\nJane,30\n\n\n"
        with open(self.output_dir / "many_empty_lines.csv", 'w') as f:
            f.write(content)
        
        # Inconsistent field counts
        content = "col1,col2,col3\na,b\nc,d,e,f\ng,h,i\n"
        with open(self.output_dir / "inconsistent_fields.csv", 'w') as f:
            f.write(content)

    def generate_unicode_test(self):
        """Generate CSV files with Unicode characters"""
        data = [
            ["name", "city", "currency", "emoji"],
            ["José", "São Paulo", "€100", "😀"],
            ["北京", "Beijing", "¥500", "🚀"],
            ["Москва", "Moscow", "₽1000", "🇷🇺"],
            ["العربية", "Cairo", "$200", "🐪"]
        ]
        self.write_csv("unicode.csv", data)
        
        # Unicode in quoted fields
        data = [
            ["text"],
            ['"Mixed: English 中文 العربية 🌍"'],
            ['"Emoji story: 🚀🌟💫"'],
            ['"Special chars: ñáéíóú"']
        ]
        self.write_csv("unicode_quoted.csv", data)

    def generate_large_files(self):
        """Generate large CSV files for performance testing"""
        # Medium file (10k rows)
        headers = ["id", "name", "email", "age", "salary", "department"]
        data = [headers]
        
        departments = ["Engineering", "Sales", "Marketing", "HR", "Finance"]
        for i in range(10000):
            row = [
                str(i),
                f"User{i}",
                f"user{i}@example.com",
                str(random.randint(22, 65)),
                str(random.randint(30000, 150000)),
                random.choice(departments)
            ]
            data.append(row)
        self.write_csv("medium_10k.csv", data)
        
        # Large file (100k rows) - only generate if requested
        if len(sys.argv) > 1 and sys.argv[1] == "--large":
            data = [headers]
            for i in range(100000):
                row = [
                    str(i),
                    f"User{i}",
                    f"user{i}@example.com", 
                    str(random.randint(22, 65)),
                    str(random.randint(30000, 150000)),
                    random.choice(departments)
                ]
                data.append(row)
            self.write_csv("large_100k.csv", data)

    def generate_malformed_csv(self):
        """Generate malformed CSV files for error testing"""
        # Unclosed quotes
        content = 'name,description\nJohn,"Unclosed quote\nJane,Normal'
        with open(self.output_dir / "unclosed_quotes.csv", 'w') as f:
            f.write(content)
            
        # Invalid escape sequences
        content = 'name,value\n"John","Val"ue"\n'
        with open(self.output_dir / "invalid_escapes.csv", 'w') as f:
            f.write(content)
            
        # Quote in middle of unquoted field
        content = 'name,value\nJo"hn,25\n'
        with open(self.output_dir / "quote_in_unquoted.csv", 'w') as f:
            f.write(content)
            
        # Binary data
        with open(self.output_dir / "binary_data.csv", 'wb') as f:
            f.write(b'name,data\ntest,\x00\x01\x02\xff\n')

    def generate_performance_tests(self):
        """Generate files specifically for performance testing"""
        # Wide CSV (many columns)
        headers = [f"col{i}" for i in range(1000)]
        row = [f"val{i}" for i in range(1000)]
        data = [headers]
        for i in range(100):
            data.append([f"row{i}_val{j}" for j in range(1000)])
        self.write_csv("wide_1000_cols.csv", data)
        
        # Mostly quoted fields
        data = [["col1", "col2", "col3"]]
        for i in range(5000):
            row = [f'"Value {i}"', f'"Another value {i}"', f'"Third value {i}"']
            data.append(row)
        self.write_csv_raw("mostly_quoted_5k.csv", data)
        
        # Mixed field sizes
        data = [["small", "medium", "large"]]
        for i in range(1000):
            small = "x"
            medium = "x" * 100
            large = "x" * 1000
            data.append([small, medium, large])
        self.write_csv("mixed_sizes.csv", data)

    def write_csv(self, filename: str, data: List[List[str]], delimiter: str = ','):
        """Write data to CSV file using Python's csv module"""
        filepath = self.output_dir / filename
        with open(filepath, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f, delimiter=delimiter)
            writer.writerows(data)

    def write_csv_raw(self, filename: str, data: List[List[str]]):
        """Write CSV data without using csv module (raw format)"""
        filepath = self.output_dir / filename
        with open(filepath, 'w', encoding='utf-8') as f:
            for row in data:
                f.write(','.join(row) + '\n')

    def write_csv_with_line_ending(self, filename: str, data: List[List[str]], line_ending: str):
        """Write CSV with specific line ending"""
        filepath = self.output_dir / filename
        with open(filepath, 'w', newline='', encoding='utf-8') as f:
            for i, row in enumerate(data):
                line = ','.join(f'"{field}"' if ',' in field or '"' in field else field for field in row)
                f.write(line)
                if i < len(data) - 1:  # Don't add line ending after last row
                    f.write(line_ending)

    def generate_test_manifest(self):
        """Generate a manifest file describing all test files"""
        manifest = {
            "basic": {
                "basic_simple.csv": "Simple 4x3 CSV with header",
                "basic_single_column.csv": "Single column CSV",
                "basic_single_row.csv": "Single row CSV", 
                "basic_no_header.csv": "CSV without header",
                "basic_many_columns.csv": "CSV with 50 columns"
            },
            "quoted": {
                "quoted_fields.csv": "CSV with quoted fields containing commas",
                "quoted_all_fields.csv": "CSV with all fields quoted",
                "quoted_mixed.csv": "CSV with mixed quoted/unquoted fields"
            },
            "escaped": {
                "escaped_quotes.csv": "CSV with escaped quotes using double quotes"
            },
            "multiline": {
                "multiline_fields.csv": "CSV with multiline fields",
                "complex_multiline.csv": "Complex multiline with quotes and commas"
            },
            "empty": {
                "empty_fields.csv": "CSV with empty fields",
                "quoted_empty_fields.csv": "CSV with quoted empty fields", 
                "trailing_empty.csv": "CSV with trailing empty fields"
            },
            "delimiters": {
                "pipe_delimited.csv": "Pipe-delimited file",
                "tab_delimited.csv": "Tab-delimited file",
                "semicolon_delimited.csv": "Semicolon-delimited file"
            },
            "edge_cases": {
                "empty.csv": "Empty file",
                "only_header.csv": "File with only header",
                "single_field.csv": "Single field, single row",
                "long_field.csv": "File with very long field",
                "inconsistent_fields.csv": "Inconsistent field counts per row"
            },
            "unicode": {
                "unicode.csv": "CSV with Unicode characters",
                "unicode_quoted.csv": "Quoted fields with Unicode"
            },
            "performance": {
                "medium_10k.csv": "10,000 rows for performance testing",
                "wide_1000_cols.csv": "1000 columns for width testing",
                "mostly_quoted_5k.csv": "5000 rows with mostly quoted fields"
            },
            "malformed": {
                "unclosed_quotes.csv": "CSV with unclosed quotes (should error)",
                "invalid_escapes.csv": "CSV with invalid escape sequences",
                "quote_in_unquoted.csv": "Quote in middle of unquoted field"
            }
        }
        
        import json
        with open(self.output_dir / "test_manifest.json", 'w') as f:
            json.dump(manifest, f, indent=2)

def main():
    generator = CSVTestDataGenerator()
    generator.generate_all_tests()
    generator.generate_test_manifest()
    
    # Print summary
    files = list(generator.output_dir.glob("*.csv"))
    total_size = sum(f.stat().st_size for f in files)
    
    print(f"\nTest Data Summary:")
    print(f"Files generated: {len(files)}")
    print(f"Total size: {total_size / 1024:.1f} KB")
    print(f"Location: {generator.output_dir.absolute()}")
    
    # Show largest files
    largest = sorted(files, key=lambda f: f.stat().st_size, reverse=True)[:5]
    print(f"\nLargest files:")
    for f in largest:
        size_kb = f.stat().st_size / 1024
        print(f"  {f.name}: {size_kb:.1f} KB")

if __name__ == "__main__":
    main()