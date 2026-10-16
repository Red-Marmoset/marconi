// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the file browser's helpers: the clickable path, sorting and type names

#include "TestSupport.h"

#include <MarconiCore/Files/FileView.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        Files::FileEntry Entry(const std::string& name, uint64_t size, int64_t modified, bool folder)
        {
            Files::FileEntry entry;
            entry.name = name;
            entry.size = size;
            entry.modified = modified;
            entry.isFolder = folder;
            return entry;
        }

        std::string Names(const std::vector<Files::FileEntry>& entries)
        {
            std::string names;
            for (const Files::FileEntry& entry : entries)
            {
                names += (names.empty() ? "" : ",") + entry.name;
            }
            return names;
        }
    } // namespace

    void RunFileViewTests()
    {
        RunTest("SplitPath gives each part of a path with where it leads", []() {
            std::vector<Files::PathPart> parts = Files::SplitPath("C:/Users/Sem");
            CHECK(parts.size() == 3);
            CHECK(parts[0].label == "C:" && parts[0].path == "C:/");
            CHECK(parts[1].label == "Users" && parts[1].path == "C:/Users");
            CHECK(parts[2].label == "Sem" && parts[2].path == "C:/Users/Sem");

            parts = Files::SplitPath("/Users/sem/");
            CHECK(parts.size() == 3 && parts[0].label == "/" && parts[0].path == "/");
            CHECK(parts[2].label == "sem" && parts[2].path == "/Users/sem");

            parts = Files::SplitPath("/C:/Users"); // TightVNC
            CHECK(parts.size() == 3 && parts[1].label == "C:" && parts[1].path == "/C:");
            CHECK(parts[2].path == "/C:/Users");

            parts = Files::SplitPath("/");
            CHECK(parts.size() == 1 && parts[0].path == "/");
        });

        RunTest("SortEntries keeps folders first and sorts by the chosen column", []() {
            std::vector<Files::FileEntry> entries = {
                Entry("b.txt", 30, 300, false), Entry("Zeta", 0, 100, true), Entry("a.PDF", 10, 200, false),
                Entry("alpha", 0, 400, true),   Entry("C.txt", 20, 100, false)};
            Files::SortEntries(entries, Files::SortColumn::Name, true);
            CHECK(Names(entries) == "alpha,Zeta,a.PDF,b.txt,C.txt");
            Files::SortEntries(entries, Files::SortColumn::Name, false);
            CHECK(Names(entries) == "Zeta,alpha,C.txt,b.txt,a.PDF");
            Files::SortEntries(entries, Files::SortColumn::Size, true);
            CHECK(Names(entries) == "alpha,Zeta,a.PDF,C.txt,b.txt");
            Files::SortEntries(entries, Files::SortColumn::Modified, false);
            CHECK(Names(entries) == "alpha,Zeta,b.txt,a.PDF,C.txt");
            Files::SortEntries(entries, Files::SortColumn::Type, true);
            CHECK(Names(entries) == "alpha,Zeta,a.PDF,b.txt,C.txt"); // PDF before TXT, then by name
        });

        RunTest("TypeName describes files in plain words", []() {
            CHECK(Files::TypeName(Entry("Photos", 0, 0, true)) == "Folder");
            CHECK(Files::TypeName(Entry("report.pdf", 1, 0, false)) == "PDF file");
            CHECK(Files::TypeName(Entry("archive.tar.gz", 1, 0, false)) == "GZ file");
            CHECK(Files::TypeName(Entry("Makefile", 1, 0, false)) == "File");
            CHECK(Files::TypeName(Entry(".bashrc", 1, 0, false)) == "File");
        });
    }
} // namespace MarconiTest
