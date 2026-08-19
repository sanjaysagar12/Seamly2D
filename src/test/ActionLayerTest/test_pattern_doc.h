//---------------------------------------------------------------------------------------------------------------------
//  @file   test_pattern_doc.h
//  @author Seamly2D Contributors
//  @date   19 Aug, 2026
//
//  @copyright
//  Copyright (C) 2017 - 2026 Seamly, LLC
//  https://github.com/fashionfreedom/seamly2d
//
//  @brief
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D. If not, see <http://www.gnu.org/licenses/>.
//---------------------------------------------------------------------------------------------------------------------

#ifndef TEST_PATTERN_DOC_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TEST_PATTERN_DOC_H // Marks TEST_PATTERN_DOC_H as defined for the remainder of the include guard.

#include "../../libs/ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern, the base class this stub extends.

class VContainer; // Forward declaration; only used by pointer in the override below.

// Minimal concrete VAbstractPattern: implements every pure virtual with a trivial, no-op body,
// since ActionLayerTest never exercises XML parsing, label generation, or reference counting --
// only getHistory(), which VAbstractPattern already implements concretely. Originally local to
// tst_pattern_dump.cpp; factored out here so every ActionLayerTest test file can build an
// ActionContext's VAbstractPattern* without duplicating this stub.
class TestPatternDoc : public VAbstractPattern
{
public:
    explicit TestPatternDoc(QObject *parent = nullptr) // Forwards straight to the base constructor.
        : VAbstractPattern(parent) // Base constructor reads default line settings via qApp, hence each test binary's app bootstrap.
    {
    }

    void CreateEmptyFile() override {} // Never called: tests build state directly, not from an empty document.

    void IncrementReferens(quint32 id) const override { Q_UNUSED(id) } // Reference counting is irrelevant to a read-only action.
    void DecrementReferens(quint32 id) const override { Q_UNUSED(id) } // Reference counting is irrelevant to a read-only action.

    QStringList GetCurrentAlphabet() const override { return QStringList(); } // No label alphabet needed for these tests.

    QString GenerateLabel(const LabelType &type, const QString &reservedName = QString()) const override
    {
        Q_UNUSED(type)         // Label generation is outside every ActionLayerTest handler's scope.
        Q_UNUSED(reservedName) // Label generation is outside every ActionLayerTest handler's scope.
        return QString();      // No label text needed for these tests.
    }

    QString GenerateSuffix(const QString &type) const override
    {
        Q_UNUSED(type)    // Suffix generation is outside every ActionLayerTest handler's scope.
        return QString(); // No suffix text needed for these tests.
    }

    void UpdateToolData(const quint32 &id, VContainer *data) override
    {
        Q_UNUSED(id)   // Tests never re-parse tool data; history is populated directly instead.
        Q_UNUSED(data) // Tests never re-parse tool data; history is populated directly instead.
    }

public slots:
    void LiteParseTree(const Document &parse) override { Q_UNUSED(parse) } // Never invoked: no XML parsing occurs in these tests.
};

#endif // TEST_PATTERN_DOC_H // End of include guard started above.
